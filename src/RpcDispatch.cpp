#include "RpcDispatch.h"

#include "amxxmodule.h"

#include "Log.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct Outstanding {
	uint64_t sessionId = 0;
	std::string rawId;
	std::chrono::steady_clock::time_point deadline;
};

Queue<RpcRequest>* g_inbox = nullptr;
Queue<RpcOut>* g_outbox = nullptr;
std::atomic<int> g_drainBudget{64};
std::atomic<int> g_requestTimeout{15};
size_t g_outboxDepth = 256;
uint32_t g_nextHandle = 1;
std::unordered_map<uint32_t, Outstanding> g_outstanding;
std::unordered_map<uint64_t, size_t> g_outPending;
std::mutex g_mutex;

std::chrono::steady_clock::time_point Now() {
	return std::chrono::steady_clock::now();
}

void PushOut(uint64_t sessionId, const std::string& bytes, bool isNotification) {
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		size_t& pending = g_outPending[sessionId];
		if (pending >= g_outboxDepth) {
			if (isNotification)
				return; // drop notifications first (M-4)
			g_outPending.erase(sessionId);
			Transport_RequestClose(sessionId); // never drop responses -> close session
			return;
		}
		++pending;
	}

	RpcOut out;
	out.sessionId = sessionId;
	out.bytes = bytes;
	out.isNotification = isNotification;
	if (!g_outbox || !g_outbox->TryPush(std::move(out))) {
		std::lock_guard<std::mutex> lock(g_mutex);
		auto it = g_outPending.find(sessionId);
		if (it != g_outPending.end()) {
			if (it->second > 0)
				--it->second;
			if (it->second == 0)
				g_outPending.erase(it);
		}
		if (!isNotification)
			Transport_RequestClose(sessionId);
		return;
	}
	Transport_Wakeup();
}

void Reply(uint32_t handle, uint64_t sessionId, const std::string& resultJson) {
	if (handle == 0)
		return;
	std::string rawId;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		auto it = g_outstanding.find(handle);
		if (it == g_outstanding.end())
			return; // late/duplicate reply ignored
		rawId = it->second.rawId;
		g_outstanding.erase(it);
	}
	PushOut(sessionId, Protocol_BuildResult(rawId, resultJson), false);
}

void ReplyError(uint32_t handle, uint64_t sessionId, int code, const std::string& message) {
	if (handle == 0)
		return;
	std::string rawId;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		auto it = g_outstanding.find(handle);
		if (it == g_outstanding.end())
			return;
		rawId = it->second.rawId;
		g_outstanding.erase(it);
	}
	PushOut(sessionId, Protocol_BuildError(rawId, code, message), false);
}

void DispatchMethod(const RpcRequest& req, uint32_t handle) {
	if (req.method == "rpc.ping") {
		Reply(handle, req.sessionId, "{\"pong\":true}");
	} else if (req.method == "rpc.version") {
		std::string result = "{\"module\":\"" MODULE_VERSION "\",\"protocol\":\"" ARP_PROTO_VERSION "\"}";
		Reply(handle, req.sessionId, result);
	} else if (req.method == "rpc.methods") {
		Reply(handle, req.sessionId,
		      "{\"methods\":[\"rpc.ping\",\"rpc.version\",\"rpc.methods\"],"
		      "\"transport\":[\"rpc.auth\"]}");
	} else {
		ReplyError(handle, req.sessionId, RPC_METHOD_NOT_FOUND, "method not found");
	}
}

void ProcessRequest(const RpcRequest& req) {
	if (req.isNotification) {
		DispatchMethod(req, 0);
		return;
	}
	uint32_t handle = g_nextHandle++;
	if (g_nextHandle == 0)
		g_nextHandle = 1;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		Outstanding outstanding;
		outstanding.sessionId = req.sessionId;
		outstanding.rawId = req.rawId;
		outstanding.deadline = Now() + std::chrono::seconds(g_requestTimeout.load());
		g_outstanding[handle] = outstanding;
	}
	DispatchMethod(req, handle);
}

void CheckTimeouts() {
	auto now = Now();
	std::vector<std::pair<uint32_t, Outstanding> > expired;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		for (auto it = g_outstanding.begin(); it != g_outstanding.end();) {
			if (now >= it->second.deadline) {
				expired.push_back(*it);
				it = g_outstanding.erase(it);
			} else {
				++it;
			}
		}
	}
	for (size_t i = 0; i < expired.size(); ++i) {
		PushOut(expired[i].second.sessionId,
		        Protocol_BuildError(expired[i].second.rawId, RPC_REQUEST_TIMEOUT, "request timeout"),
		        false);
	}
}

} // namespace

void Rpc_Init(const Config& cfg, Queue<RpcRequest>* inbox, Queue<RpcOut>* outbox) {
	g_inbox = inbox;
	g_outbox = outbox;
	g_drainBudget.store(cfg.drainBudget);
	g_requestTimeout.store(cfg.requestTimeout);
	g_outboxDepth = (size_t)cfg.outboxDepth;
	g_nextHandle = 1;
	std::lock_guard<std::mutex> lock(g_mutex);
	g_outstanding.clear();
	g_outPending.clear();
}

void Rpc_Tick() {
	if (!g_inbox)
		return;
	int budget = g_drainBudget.load();
	RpcRequest req;
	for (int i = 0; i < budget; ++i) {
		if (!g_inbox->TryPop(req))
			break;
		ProcessRequest(req);
	}
	CheckTimeouts();
}

void Rpc_Shutdown() {
	std::lock_guard<std::mutex> lock(g_mutex);
	g_outstanding.clear();
	g_outPending.clear();
}

void Rpc_ApplyLive(const Config& cfg) {
	g_drainBudget.store(cfg.drainBudget);
	g_requestTimeout.store(cfg.requestTimeout);
}

void Rpc_OnOutboxDrained(uint64_t sessionId) {
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_outPending.find(sessionId);
	if (it != g_outPending.end()) {
		if (it->second > 0)
			--it->second;
		if (it->second == 0)
			g_outPending.erase(it);
	}
}

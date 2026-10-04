#include "RpcDispatch.h"

#include "amxxmodule.h"

#include "CoreMethods.h"
#include "Events.h"
#include "Log.h"
#include "PawnApi.h"

#include <atomic>
#include <chrono>
#include <memory>
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
	std::string methodName;
	int pluginId = -1;
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
std::unique_ptr<RpcRegistry> g_registry;
int g_pruneCounter = 0;

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

// Shared reply path. When checkOwner is true the outstanding entry must belong
// to requiredPluginId, otherwise the reply is ignored.
bool ReplyInternal(uint32_t handle, const std::string& resultJson,
                   int requiredPluginId, bool checkOwner) {
	if (handle == 0)
		return false;
	uint64_t sessionId = 0;
	std::string rawId;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		auto it = g_outstanding.find(handle);
		if (it == g_outstanding.end())
			return false; // late/duplicate reply ignored
		if (checkOwner && it->second.pluginId != requiredPluginId)
			return false; // not the owner
		sessionId = it->second.sessionId;
		rawId = it->second.rawId;
		g_outstanding.erase(it);
	}
	PushOut(sessionId, Protocol_BuildResult(rawId, resultJson), false);
	return true;
}

bool ReplyErrorInternal(uint32_t handle, int code, const std::string& message,
                        int requiredPluginId, bool checkOwner) {
	if (handle == 0)
		return false;
	uint64_t sessionId = 0;
	std::string rawId;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		auto it = g_outstanding.find(handle);
		if (it == g_outstanding.end())
			return false;
		if (checkOwner && it->second.pluginId != requiredPluginId)
			return false;
		sessionId = it->second.sessionId;
		rawId = it->second.rawId;
		g_outstanding.erase(it);
	}
	PushOut(sessionId, Protocol_BuildError(rawId, code, message), false);
	return true;
}

void DispatchMethod(const RpcRequest& req, uint32_t handle) {
	RpcRegistry* registry = g_registry.get();
	const RpcMethodInfo* info = registry ? registry->Find(req.method) : nullptr;
	if (!info) {
		Rpc_ReplyError(handle, RPC_METHOD_NOT_FOUND, "method not found");
		return;
	}
	if (info->source == RPC_SOURCE_BUILTIN) {
		if (!Core_Dispatch(req.method, req.paramsJson, handle, req.sessionId))
			Rpc_ReplyError(handle, RPC_METHOD_NOT_FOUND, "method not found");
	} else if (info->source == RPC_SOURCE_PAWN) {
		Pawn_Dispatch(*info, req, handle);
	} else {
		// transport methods (rpc.auth) are handled on the I/O thread
		Rpc_ReplyError(handle, RPC_METHOD_NOT_FOUND, "method not found");
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
		outstanding.methodName = req.method;
		outstanding.pluginId = -1;
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
	g_registry.reset(new RpcRegistry());
	g_pruneCounter = 0;
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
	if (++g_pruneCounter >= 128) {
		g_pruneCounter = 0;
		Events_Prune();
	}
}

void Rpc_Shutdown() {
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_outstanding.clear();
		g_outPending.clear();
	}
	g_registry.reset();
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

RpcRegistry* Rpc_GetRegistry() {
	return g_registry.get();
}

bool Rpc_Reply(uint32_t handle, const std::string& resultJson) {
	return ReplyInternal(handle, resultJson, -1, false);
}

bool Rpc_ReplyError(uint32_t handle, int code, const std::string& message) {
	return ReplyErrorInternal(handle, code, message, -1, false);
}

bool Rpc_ReplyFromPlugin(uint32_t handle, int pluginId, const std::string& resultJson) {
	return ReplyInternal(handle, resultJson, pluginId, true);
}

bool Rpc_ReplyErrorFromPlugin(uint32_t handle, int pluginId, int code,
                              const std::string& message) {
	return ReplyErrorInternal(handle, code, message, pluginId, true);
}

void Rpc_SetOutstandingPlugin(uint32_t handle, int pluginId) {
	if (handle == 0)
		return;
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_outstanding.find(handle);
	if (it != g_outstanding.end())
		it->second.pluginId = pluginId;
}

void Rpc_FailPawnRequests(int code, const std::string& message) {
	std::vector<std::pair<uint64_t, std::string> > failed;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		for (auto it = g_outstanding.begin(); it != g_outstanding.end();) {
			if (it->second.pluginId >= 0) {
				failed.push_back(std::make_pair(it->second.sessionId, it->second.rawId));
				it = g_outstanding.erase(it);
			} else {
				++it;
			}
		}
	}
	for (size_t i = 0; i < failed.size(); ++i)
		PushOut(failed[i].first, Protocol_BuildError(failed[i].second, code, message), false);
}

void Rpc_PushNotification(uint64_t sessionId, const std::string& method,
                          const std::string& paramsJson) {
	PushOut(sessionId, Protocol_BuildNotification(method, paramsJson), true);
}

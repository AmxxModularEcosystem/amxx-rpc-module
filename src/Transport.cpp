#include "Transport.h"

#include "Log.h"
#include "RpcDispatch.h"
#include "parson.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#if defined(_WIN32)
	#include <winsock2.h>
	#include <ws2tcpip.h>
	typedef int socklen_t;
	#define SOCK_ERRNO WSAGetLastError()
	#define SOCK_EWOULDBLOCK WSAEWOULDBLOCK
	#define SOCK_EINTR WSAEINTR
	#define SOCK_EMFILE WSAEMFILE
	#define SOCK_ENFILE WSAEMFILE
	#define CLOSE_SOCKET closesocket
#else
	#include <arpa/inet.h>
	#include <errno.h>
	#include <fcntl.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <signal.h>
	#include <sys/select.h>
	#include <sys/socket.h>
	#include <unistd.h>
	#define SOCK_ERRNO errno
	#define SOCK_EWOULDBLOCK EWOULDBLOCK
	#define SOCK_EINTR EINTR
	#define SOCK_EMFILE EMFILE
	#define SOCK_ENFILE ENFILE
	#define CLOSE_SOCKET close
#endif

#if defined(MSG_NOSIGNAL)
	#define SEND_FLAGS MSG_NOSIGNAL
#else
	#define SEND_FLAGS 0
#endif

namespace {

struct Session {
	uint64_t id = 0;
	int fd = -1;
	bool authenticated = false;
	std::string inbuf;
	std::string outbuf;
	std::chrono::steady_clock::time_point lastActivity;
	std::chrono::steady_clock::time_point createdAt;
};

struct TransportState {
	std::thread thread;
	std::atomic<bool> running{false};
	std::atomic<bool> stopRequested{false};
	int listenFd = -1;
	int wakeupRecv = -1;
	int wakeupSend = -1;
	sockaddr_in wakeupAddr{};
	std::atomic<uint64_t> nextSessionId{1};
	std::atomic<int> clientCount{0};
	std::mutex closeMutex;
	std::set<uint64_t> closeRequests;
	Queue<RpcRequest>* inbox = nullptr;
	Queue<RpcOut>* outbox = nullptr;
	std::atomic<int> idleTimeout{120};
	std::atomic<int> preAuthTimeout{5};
	std::atomic<int> maxConnections{4};
	std::atomic<long long> maxMessageBytes{1048576};
	std::atomic<long long> maxSessionOutBytes{1048576};
	std::atomic<bool> closeAllRequested{false};
	std::mutex tokenMutex;
	std::string token;
	std::mutex lifecycleMutex;
	std::mutex authMutex;
	std::set<uint64_t> authSessions;
};

TransportState g_state;

std::chrono::steady_clock::time_point Now() {
	return std::chrono::steady_clock::now();
}

void ForgetAuthSession(uint64_t id) {
	std::lock_guard<std::mutex> lock(g_state.authMutex);
	g_state.authSessions.erase(id);
}

void SetNonBlocking(int fd) {
#if defined(_WIN32)
	u_long mode = 1;
	ioctlsocket(fd, FIONBIO, &mode);
#else
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

void SetNoDelay(int fd) {
	int yes = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&yes, sizeof(yes));
}

bool FlushSession(Session& s) {
	while (!s.outbuf.empty()) {
		int n = send(s.fd, s.outbuf.data(), (int)s.outbuf.size(), SEND_FLAGS);
		if (n > 0) {
			s.outbuf.erase(0, (size_t)n);
			s.lastActivity = Now();
		} else if (n < 0 && SOCK_ERRNO == SOCK_EWOULDBLOCK) {
			break;
		} else if (n < 0 && SOCK_ERRNO == SOCK_EINTR) {
			continue;
		} else {
			return false;
		}
	}
	return true;
}

bool SendBytes(Session& s, const std::string& bytes) {
	s.outbuf += bytes;
	s.outbuf += '\n';
	if ((long long)s.outbuf.size() > g_state.maxSessionOutBytes.load()) {
		Log_Write(ARP_LOG_WARN, "session %llu output buffer overflow; closing",
		          (unsigned long long)s.id);
		return false;
	}
	return FlushSession(s);
}

bool SendError(Session& s, const std::string& rawId, int code, const std::string& message) {
	return SendBytes(s, Protocol_BuildError(rawId, code, message));
}

bool ConstantTimeEquals(const std::string& a, const std::string& b) {
	size_t maxLen = a.size() > b.size() ? a.size() : b.size();
	unsigned char diff = (a.size() == b.size()) ? 0 : 1;
	for (size_t i = 0; i < maxLen; ++i) {
		unsigned char ca = i < a.size() ? (unsigned char)a[i] : 0;
		unsigned char cb = i < b.size() ? (unsigned char)b[i] : 0;
		diff |= (unsigned char)(ca ^ cb);
	}
	return diff == 0;
}

bool ExtractToken(const std::string& paramsJson, std::string& token) {
	if (paramsJson.empty())
		return false;
	JSON_Value* root = json_parse_string(paramsJson.c_str());
	if (!root)
		return false;
	bool ok = false;
	if (json_value_get_type(root) == JSONObject) {
		const char* value = json_object_get_string(json_value_get_object(root), "token");
		if (value) {
			token = value;
			ok = true;
		}
	}
	json_value_free(root);
	return ok;
}

bool HandleAuth(Session& s, const std::string& line) {
	ProtocolParseResult pr = Protocol_ParseRequest(
	    line, (size_t)g_state.maxMessageBytes.load(), ARP_MAX_JSON_DEPTH);
	if (!pr.ok) {
		SendError(s, "null", pr.errorCode, pr.errorMessage);
		return false;
	}
	if (pr.request.method != "rpc.auth") {
		SendError(s, pr.request.rawId, RPC_NOT_AUTHENTICATED, "not authenticated");
		return false;
	}
	std::string token;
	if (!ExtractToken(pr.request.paramsJson, token)) {
		SendError(s, pr.request.rawId, RPC_INVALID_PARAMS, "missing token");
		return false;
	}
	bool tokenMatches = false;
	{
		std::lock_guard<std::mutex> lock(g_state.tokenMutex);
		tokenMatches = ConstantTimeEquals(token, g_state.token);
	}
	if (!tokenMatches) {
		Log_Write(ARP_LOG_WARN, "session %llu auth failed", (unsigned long long)s.id);
		SendError(s, pr.request.rawId, RPC_NOT_AUTHENTICATED, "invalid token");
		return false;
	}
	s.authenticated = true;
	s.lastActivity = Now();
	{
		std::lock_guard<std::mutex> lock(g_state.authMutex);
		g_state.authSessions.insert(s.id);
	}
	Log_Write(ARP_LOG_INFO, "session %llu authenticated", (unsigned long long)s.id);
	return SendBytes(s, Protocol_BuildResult(pr.request.rawId, "{\"ok\":true}"));
}

bool ProcessLine(Session& s, const std::string& line) {
	if (!s.authenticated)
		return HandleAuth(s, line);

	ProtocolParseResult pr = Protocol_ParseRequest(
	    line, (size_t)g_state.maxMessageBytes.load(), ARP_MAX_JSON_DEPTH);
	if (!pr.ok) {
		if (pr.isBatch) {
			SendError(s, "null", RPC_INVALID_REQUEST, pr.errorMessage);
			return true; // batch rejected, keep the connection
		}
		SendError(s, "null", pr.errorCode, pr.errorMessage);
		return false; // parse/invalid request -> close
	}
	if (pr.request.method == "rpc.auth")
		return HandleAuth(s, line);

	RpcRequest req = pr.request;
	req.sessionId = s.id;
	if (req.isNotification) {
		if (!g_state.inbox || !g_state.inbox->TryPush(std::move(req)))
			Log_Write(ARP_LOG_WARN, "inbox full; notification dropped");
		return true;
	}
	std::string rawId = req.rawId;
	if (!g_state.inbox || !g_state.inbox->TryPush(std::move(req)))
		SendError(s, rawId, RPC_SERVER_BUSY, "server busy");
	return true;
}

bool ProcessInput(Session& s) {
	int processed = 0;
	size_t pos;
	while ((pos = s.inbuf.find('\n')) != std::string::npos) {
		std::string line = s.inbuf.substr(0, pos);
		s.inbuf.erase(0, pos + 1);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;
		if ((long long)line.size() > g_state.maxMessageBytes.load()) {
			Log_Write(ARP_LOG_WARN, "session %llu oversize message; closing",
			          (unsigned long long)s.id);
			return false;
		}
		if (!ProcessLine(s, line))
			return false;
		if (++processed >= 64)
			break; // fairness budget per socket per iteration
	}
	return true;
}

bool HandleRead(Session& s) {
	char buf[4096];
	for (;;) {
		int n = recv(s.fd, buf, (int)sizeof(buf), 0);
		if (n > 0) {
			s.lastActivity = Now();
			s.inbuf.append(buf, (size_t)n);
			if (!ProcessInput(s))
				return false;
			if (s.inbuf.size() > (size_t)g_state.maxMessageBytes.load() &&
			    s.inbuf.find('\n') == std::string::npos) {
				Log_Write(ARP_LOG_WARN, "session %llu oversize accumulation; closing",
				          (unsigned long long)s.id);
				return false;
			}
		} else if (n == 0) {
			return false;
		} else {
			int err = SOCK_ERRNO;
			if (err == SOCK_EWOULDBLOCK)
				break;
			if (err == SOCK_EINTR)
				continue;
			return false;
		}
	}
	return true;
}

void CloseSession(std::map<uint64_t, Session>& sessions,
                  std::map<uint64_t, Session>::iterator it) {
	int fd = it->second.fd;
	uint64_t id = it->second.id;
	sessions.erase(it);
	CLOSE_SOCKET(fd);
	ForgetAuthSession(id);
	g_state.clientCount.fetch_sub(1);
	Log_Write(ARP_LOG_INFO, "session %llu closed", (unsigned long long)id);
}

void AcceptLoop(std::map<uint64_t, Session>& sessions) {
	for (;;) {
		sockaddr_in addr;
		socklen_t addrLen = sizeof(addr);
		int fd = accept(g_state.listenFd, (sockaddr*)&addr, &addrLen);
		if (fd < 0) {
			int err = SOCK_ERRNO;
			if (err == SOCK_EWOULDBLOCK)
				break;
			if (err == SOCK_EINTR)
				continue;
			if (err == SOCK_EMFILE || err == SOCK_ENFILE) {
				Log_Write(ARP_LOG_ERROR, "accept: too many open files; backing off");
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				break;
			}
			Log_Write(ARP_LOG_ERROR, "accept failed (%d)", err);
			break;
		}
		if ((int)sessions.size() >= g_state.maxConnections.load()) {
			Log_Write(ARP_LOG_WARN, "connection refused: max_connections reached");
			CLOSE_SOCKET(fd);
			continue;
		}
		SetNonBlocking(fd);
		SetNoDelay(fd);
		uint64_t id = g_state.nextSessionId.fetch_add(1);
		Session s;
		s.id = id;
		s.fd = fd;
		s.authenticated = false;
		s.lastActivity = Now();
		s.createdAt = Now();
		sessions.emplace(id, std::move(s));
		g_state.clientCount.fetch_add(1);
		Log_Write(ARP_LOG_INFO, "session %llu accepted", (unsigned long long)id);
	}
}

void DrainOutbox(std::map<uint64_t, Session>& sessions) {
	if (!g_state.outbox)
		return;
	RpcOut out;
	while (g_state.outbox->TryPop(out)) {
		auto it = sessions.find(out.sessionId);
		if (it != sessions.end()) {
			it->second.outbuf += out.bytes;
			if ((long long)it->second.outbuf.size() > g_state.maxSessionOutBytes.load()) {
				Log_Write(ARP_LOG_WARN, "session %llu output overflow; closing",
				          (unsigned long long)out.sessionId);
				CloseSession(sessions, it);
			}
		}
		Rpc_OnOutboxDrained(out.sessionId);
	}
}

void ProcessCloseRequests(std::map<uint64_t, Session>& sessions) {
	if (g_state.closeAllRequested.exchange(false)) {
		for (auto it = sessions.begin(); it != sessions.end();) {
			int fd = it->second.fd;
			uint64_t id = it->second.id;
			it = sessions.erase(it);
			CLOSE_SOCKET(fd);
			ForgetAuthSession(id);
			g_state.clientCount.fetch_sub(1);
			Log_Write(ARP_LOG_INFO, "session %llu invalidated", (unsigned long long)id);
		}
	}
	std::set<uint64_t> requests;
	{
		std::lock_guard<std::mutex> lock(g_state.closeMutex);
		if (g_state.closeRequests.empty())
			return;
		requests.swap(g_state.closeRequests);
	}
	for (uint64_t id : requests) {
		auto it = sessions.find(id);
		if (it != sessions.end())
			CloseSession(sessions, it);
	}
}

void CheckTimeouts(std::map<uint64_t, Session>& sessions) {
	auto now = Now();
	int idle = g_state.idleTimeout.load();
	int preAuth = g_state.preAuthTimeout.load();
	for (auto it = sessions.begin(); it != sessions.end();) {
		Session& s = it->second;
		bool dead = false;
		if (!s.authenticated) {
			auto age = std::chrono::duration_cast<std::chrono::seconds>(now - s.createdAt).count();
			if (age >= preAuth)
				dead = true;
		}
		if (!dead) {
			auto idleAge = std::chrono::duration_cast<std::chrono::seconds>(now - s.lastActivity).count();
			if (idleAge >= idle)
				dead = true;
		}
		if (dead) {
			int fd = s.fd;
			uint64_t id = s.id;
			it = sessions.erase(it);
			CLOSE_SOCKET(fd);
			ForgetAuthSession(id);
			g_state.clientCount.fetch_sub(1);
			Log_Write(ARP_LOG_INFO, "session %llu timed out", (unsigned long long)id);
		} else {
			++it;
		}
	}
}

void DrainWakeup() {
	char buf[64];
	for (;;) {
		int n = recvfrom(g_state.wakeupRecv, buf, (int)sizeof(buf), 0, nullptr, nullptr);
		if (n <= 0)
			break;
	}
}

void IoThreadMain() {
	std::map<uint64_t, Session> sessions;
	while (!g_state.stopRequested.load()) {
		DrainOutbox(sessions);
		ProcessCloseRequests(sessions);

		fd_set readSet;
		fd_set writeSet;
		FD_ZERO(&readSet);
		FD_ZERO(&writeSet);
		int maxFd = -1;
		auto addFd = [&maxFd](int fd, fd_set* set) {
			FD_SET(fd, set);
			if (fd > maxFd)
				maxFd = fd;
		};
		if (g_state.wakeupRecv >= 0)
			addFd(g_state.wakeupRecv, &readSet);
		if (g_state.listenFd >= 0)
			addFd(g_state.listenFd, &readSet);
		for (auto& kv : sessions) {
			addFd(kv.second.fd, &readSet);
			if (!kv.second.outbuf.empty())
				addFd(kv.second.fd, &writeSet);
		}

		timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
		int ready = select(maxFd + 1, &readSet, &writeSet, nullptr, &tv);
		if (ready < 0) {
			if (SOCK_ERRNO == SOCK_EINTR)
				continue;
			Log_Write(ARP_LOG_ERROR, "select failed (%d)", SOCK_ERRNO);
			continue;
		}
		if (ready == 0) {
			CheckTimeouts(sessions);
			continue;
		}

		if (g_state.wakeupRecv >= 0 && FD_ISSET(g_state.wakeupRecv, &readSet))
			DrainWakeup();
		if (g_state.listenFd >= 0 && FD_ISSET(g_state.listenFd, &readSet))
			AcceptLoop(sessions);

		for (auto it = sessions.begin(); it != sessions.end();) {
			Session& s = it->second;
			bool dead = false;
			if (FD_ISSET(s.fd, &readSet)) {
				if (!HandleRead(s))
					dead = true;
			}
			if (!dead && FD_ISSET(s.fd, &writeSet)) {
				if (!FlushSession(s))
					dead = true;
			}
			if (dead) {
				int fd = s.fd;
				uint64_t id = s.id;
				it = sessions.erase(it);
				CLOSE_SOCKET(fd);
				ForgetAuthSession(id);
				g_state.clientCount.fetch_sub(1);
				Log_Write(ARP_LOG_INFO, "session %llu closed", (unsigned long long)id);
			} else {
				++it;
			}
		}
		CheckTimeouts(sessions);
	}

	for (auto& kv : sessions)
		CLOSE_SOCKET(kv.second.fd);
	sessions.clear();
	g_state.clientCount.store(0);
	{
		std::lock_guard<std::mutex> lock(g_state.authMutex);
		g_state.authSessions.clear();
	}
}

bool CreateWakeup() {
	g_state.wakeupRecv = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_state.wakeupRecv < 0)
		return false;
	sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;
	if (bind(g_state.wakeupRecv, (sockaddr*)&addr, sizeof(addr)) < 0) {
		CLOSE_SOCKET(g_state.wakeupRecv);
		g_state.wakeupRecv = -1;
		return false;
	}
	socklen_t len = sizeof(addr);
	if (getsockname(g_state.wakeupRecv, (sockaddr*)&addr, &len) < 0) {
		CLOSE_SOCKET(g_state.wakeupRecv);
		g_state.wakeupRecv = -1;
		return false;
	}
	g_state.wakeupAddr = addr;
	SetNonBlocking(g_state.wakeupRecv);
	g_state.wakeupSend = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_state.wakeupSend < 0) {
		CLOSE_SOCKET(g_state.wakeupRecv);
		g_state.wakeupRecv = -1;
		return false;
	}
	SetNonBlocking(g_state.wakeupSend);
	return true;
}

bool CreateListen(const Config& cfg, int& outFd, std::string& error) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		error = "socket() failed";
		return false;
	}
	int yes = 1;
#if defined(_WIN32)
	setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&yes, sizeof(yes));
#else
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#endif
	sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)cfg.port);
	if (cfg.host.empty() || cfg.host == "localhost") {
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	} else if (inet_pton(AF_INET, cfg.host.c_str(), &addr.sin_addr) != 1) {
		error = "invalid host: " + cfg.host;
		CLOSE_SOCKET(fd);
		return false;
	}
	if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
		error = "bind failed on " + cfg.host + ":" + std::to_string(cfg.port);
		CLOSE_SOCKET(fd);
		return false;
	}
	if (listen(fd, SOMAXCONN) < 0) {
		error = "listen failed";
		CLOSE_SOCKET(fd);
		return false;
	}
	SetNonBlocking(fd);
	outFd = fd;
	return true;
}

void ApplyConfig(const Config& cfg) {
	g_state.idleTimeout.store(cfg.idleTimeout);
	g_state.preAuthTimeout.store(cfg.preAuthTimeout);
	g_state.maxConnections.store(cfg.maxConnections);
	g_state.maxMessageBytes.store(cfg.maxMessageBytes);
	g_state.maxSessionOutBytes.store(cfg.maxSessionOutBytes);
	std::lock_guard<std::mutex> lock(g_state.tokenMutex);
	g_state.token = cfg.token;
}

bool StartThread(int listenFd, const Config& cfg) {
	g_state.listenFd = listenFd;
	if (!CreateWakeup()) {
		Log_Write(ARP_LOG_ERROR, "failed to create wakeup channel");
		CLOSE_SOCKET(listenFd);
		g_state.listenFd = -1;
		return false;
	}
	g_state.stopRequested.store(false);
	g_state.running.store(true);
	g_state.thread = std::thread(IoThreadMain);
	Log_Write(ARP_LOG_INFO, "listening on %s:%d", cfg.host.c_str(), cfg.port);
	return true;
}

void CloseTransportSockets() {
	if (g_state.listenFd >= 0) {
		CLOSE_SOCKET(g_state.listenFd);
		g_state.listenFd = -1;
	}
	if (g_state.wakeupRecv >= 0) {
		CLOSE_SOCKET(g_state.wakeupRecv);
		g_state.wakeupRecv = -1;
	}
	if (g_state.wakeupSend >= 0) {
		CLOSE_SOCKET(g_state.wakeupSend);
		g_state.wakeupSend = -1;
	}
}

} // namespace

bool Transport_Start(const Config& cfg, Queue<RpcRequest>* inbox, Queue<RpcOut>* outbox) {
	std::lock_guard<std::mutex> lock(g_state.lifecycleMutex);
	if (g_state.running.load())
		return true;
#if defined(_WIN32)
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		Log_Write(ARP_LOG_ERROR, "WSAStartup failed");
		return false;
	}
#else
	signal(SIGPIPE, SIG_IGN);
#endif
	int fd = -1;
	std::string error;
	if (!CreateListen(cfg, fd, error)) {
		Log_Write(ARP_LOG_ERROR, "transport: %s", error.c_str());
		return false;
	}
	g_state.inbox = inbox;
	g_state.outbox = outbox;
	ApplyConfig(cfg);
	return StartThread(fd, cfg);
}

void Transport_Stop() {
	std::lock_guard<std::mutex> lock(g_state.lifecycleMutex);
	if (!g_state.running.load())
		return;
	g_state.stopRequested.store(true);
	Transport_Wakeup();
	if (g_state.thread.joinable())
		g_state.thread.join();
	CloseTransportSockets();
	g_state.running.store(false);
	g_state.stopRequested.store(false);
#if defined(_WIN32)
	WSACleanup();
#endif
	Log_Write(ARP_LOG_INFO, "transport stopped");
}

void Transport_Wakeup() {
	if (g_state.wakeupSend < 0)
		return;
	char byte = 1;
	sendto(g_state.wakeupSend, &byte, 1, 0, (sockaddr*)&g_state.wakeupAddr,
	       sizeof(g_state.wakeupAddr));
}

bool Transport_IsRunning() {
	return g_state.running.load();
}

void Transport_RequestClose(uint64_t sessionId) {
	std::lock_guard<std::mutex> lock(g_state.closeMutex);
	g_state.closeRequests.insert(sessionId);
	Transport_Wakeup();
}

void Transport_ApplyLive(const Config& cfg) {
	g_state.idleTimeout.store(cfg.idleTimeout);
	g_state.maxConnections.store(cfg.maxConnections);
}

void Transport_ApplyToken(const std::string& token) {
	{
		std::lock_guard<std::mutex> lock(g_state.tokenMutex);
		g_state.token = token;
	}
	g_state.closeAllRequested.store(true);
	Transport_Wakeup();
}

bool Transport_Restart(const Config& cfg) {
	std::lock_guard<std::mutex> lock(g_state.lifecycleMutex);
	int fd = -1;
	std::string error;
	if (!CreateListen(cfg, fd, error)) {
		Log_Write(ARP_LOG_ERROR, "reload: %s; keeping current transport", error.c_str());
		return false;
	}
	if (g_state.running.load()) {
		g_state.stopRequested.store(true);
		Transport_Wakeup();
		if (g_state.thread.joinable())
			g_state.thread.join();
		CloseTransportSockets();
		g_state.running.store(false);
		g_state.stopRequested.store(false);
	}
	ApplyConfig(cfg);
	return StartThread(fd, cfg);
}

int Transport_ClientCount() {
	return g_state.clientCount.load();
}

int Transport_AuthenticatedCount() {
	std::lock_guard<std::mutex> lock(g_state.authMutex);
	return (int)g_state.authSessions.size();
}

bool Transport_IsSessionAlive(uint64_t sessionId) {
	std::lock_guard<std::mutex> lock(g_state.authMutex);
	return g_state.authSessions.count(sessionId) != 0;
}

std::string Transport_StatusLine() {
	std::string line = g_state.running.load() ? "running" : "stopped";
	line += " clients=" + std::to_string(g_state.clientCount.load());
	return line;
}

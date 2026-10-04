#ifndef AMXXRPC_TRANSPORT_H
#define AMXXRPC_TRANSPORT_H

// TCP transport: one non-blocking I/O thread, select() over listen + sessions +
// a wakeup channel (design/11 §3–6). The I/O thread never calls AMXX/engine.

#include "Config.h"
#include "Protocol.h"
#include "Queue.h"

#include <cstdint>
#include <string>

// main -> I/O message. isNotification lets the dispatcher drop notifications
// first when a session's outbox is full (M-4).
struct RpcOut {
	uint64_t sessionId = 0;
	std::string bytes;
	bool isNotification = false;
};

// Starts the transport. Returns false on bind/listen failure (fail-fast).
bool Transport_Start(const Config& cfg, Queue<RpcRequest>* inbox, Queue<RpcOut>* outbox);

// Idempotent stop: forbid accept, wake the I/O thread, join, close sockets.
void Transport_Stop();

// Wakes the I/O thread (outbox non-empty / shutdown).
void Transport_Wakeup();

bool Transport_IsRunning();

// Asks the I/O thread to close a session (used when its outbox overflows).
void Transport_RequestClose(uint64_t sessionId);

// Applies live-reloadable keys (idle_timeout, max_connections).
void Transport_ApplyLive(const Config& cfg);

// Updates the token and invalidates all sessions (design/16 §3).
void Transport_ApplyToken(const std::string& token);

// Pre-binds the new listen socket; only on success stops the old transport and
// starts the new one (M-5). Returns false and keeps the old transport on failure.
bool Transport_Restart(const Config& cfg);

int Transport_ClientCount();
std::string Transport_StatusLine();

#endif // AMXXRPC_TRANSPORT_H

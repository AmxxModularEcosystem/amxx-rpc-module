#ifndef AMXXRPC_RPCDISPATCH_H
#define AMXXRPC_RPCDISPATCH_H

// Main-thread dispatcher: drains the I/O->main inbox in StartFrame with a budget,
// tracks outstanding requests (handle/id table, raw id, method, owner plugin),
// enforces request_timeout and pushes responses into the per-session bounded
// outbox (design/11 §6, design/12 §2–3).

#include "Config.h"
#include "Protocol.h"
#include "Queue.h"
#include "RpcRegistry.h"
#include "Transport.h"

#include <cstdint>
#include <string>

void Rpc_Init(const Config& cfg, Queue<RpcRequest>* inbox, Queue<RpcOut>* outbox);
void Rpc_Tick();
void Rpc_Shutdown();
void Rpc_ApplyLive(const Config& cfg);

// Called by the I/O thread after it drains one RpcOut (per-session accounting).
void Rpc_OnOutboxDrained(uint64_t sessionId);

// The method registry (owned by the dispatcher). Null before Rpc_Init.
RpcRegistry* Rpc_GetRegistry();

// Core/builtin replies. handle==0 (notification) is a no-op. Returns false when
// the handle is unknown (late/duplicate reply).
bool Rpc_Reply(uint32_t handle, const std::string& resultJson);
bool Rpc_ReplyError(uint32_t handle, int code, const std::string& message);

// Plugin-owned replies: accepted only when the handle belongs to a request
// dispatched to the same pluginId (design/12 §5, ownership check).
bool Rpc_ReplyFromPlugin(uint32_t handle, int pluginId, const std::string& resultJson);
bool Rpc_ReplyErrorFromPlugin(uint32_t handle, int pluginId, int code,
                              const std::string& message);

// Records the owning plugin for an outstanding request (before forward exec).
void Rpc_SetOutstandingPlugin(uint32_t handle, int pluginId);

// Fails all outstanding requests owned by Pawn plugins (global unload).
void Rpc_FailPawnRequests(int code, const std::string& message);

// Pushes a JSON-RPC notification to a session (main thread).
void Rpc_PushNotification(uint64_t sessionId, const std::string& method,
                          const std::string& paramsJson);

#endif // AMXXRPC_RPCDISPATCH_H

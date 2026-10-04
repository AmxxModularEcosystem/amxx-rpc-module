#ifndef AMXXRPC_RPCDISPATCH_H
#define AMXXRPC_RPCDISPATCH_H

// Main-thread dispatcher: drains the I/O->main inbox in StartFrame with a budget,
// tracks outstanding requests (handle/id table, raw id), enforces request_timeout
// and pushes responses into the per-session bounded outbox (design/11 §6, §8).

#include "Config.h"
#include "Protocol.h"
#include "Queue.h"
#include "Transport.h"

#include <cstdint>

void Rpc_Init(const Config& cfg, Queue<RpcRequest>* inbox, Queue<RpcOut>* outbox);
void Rpc_Tick();
void Rpc_Shutdown();
void Rpc_ApplyLive(const Config& cfg);

// Called by the I/O thread after it drains one RpcOut (per-session accounting).
void Rpc_OnOutboxDrained(uint64_t sessionId);

#endif // AMXXRPC_RPCDISPATCH_H

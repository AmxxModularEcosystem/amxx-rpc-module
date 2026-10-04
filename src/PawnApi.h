#ifndef AMXXRPC_PAWNAPI_H
#define AMXXRPC_PAWNAPI_H

// Pawn-facing natives (design/12 §4–5). Natives are registered as a module
// native table via MF_AddNatives in OnAmxxAttach; handlers run on the main
// thread. This header stays free of AMXX includes.

#include "Protocol.h"
#include "RpcRegistry.h"

#include <cstdint>

void Pawn_Init();     // installs the forward registrar and registers natives
void Pawn_Shutdown(); // unregisters all Pawn SP forwards
void Pawn_OnPluginsUnloading();

// Executes the owner plugin's SP forward for a Pawn method.
void Pawn_Dispatch(const RpcMethodInfo& info, const RpcRequest& req, uint32_t handle);

#endif // AMXXRPC_PAWNAPI_H

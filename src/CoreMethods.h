#ifndef AMXXRPC_COREMETHODS_H
#define AMXXRPC_COREMETHODS_H

// Built-in core methods (design/12 §6). Registered into the RPC registry and
// dispatched on the main thread, where engine/AMXX calls are allowed.

#include <cstdint>
#include <string>

// Registers the builtin/transport methods into the registry (Rpc_GetRegistry()).
void Core_Init();
void Core_Shutdown();

// Handles a builtin method. Returns true when the method was recognized.
bool Core_Dispatch(const std::string& method, const std::string& paramsJson,
                   uint32_t handle, uint64_t sessionId);

#endif // AMXXRPC_COREMETHODS_H

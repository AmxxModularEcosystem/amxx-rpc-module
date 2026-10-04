#ifndef AMXXRPC_FAKEMETHODS_H
#define AMXXRPC_FAKEMETHODS_H

// RPC methods for the fake-player subsystem (design/13 §6). Registered as
// builtins in the RPC registry; Core_Dispatch delegates unknown fake.* names
// here. All handlers run on the main thread.

#include <cstdint>
#include <string>

void FakeMethods_Init();

// Handles a fake.* method. Returns true when the method was recognized.
bool Fake_Dispatch(const std::string& method, const std::string& paramsJson, uint32_t handle);

#endif // AMXXRPC_FAKEMETHODS_H

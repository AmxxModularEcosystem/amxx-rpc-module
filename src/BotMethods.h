#ifndef AMXXRPC_BOTMETHODS_H
#define AMXXRPC_BOTMETHODS_H

// RPC methods for the optional YAPB adapter (design/14 §4). Registered as
// builtins in the RPC registry; Core_Dispatch delegates bot.* names here. All
// handlers run on the main thread. Without YAPB every method except
// bot.available degrades to -32002.

#include <cstdint>
#include <string>

void BotMethods_Init();

// Handles a bot.* method. Returns true when the method was recognized.
bool Bot_Dispatch(const std::string& method, const std::string& paramsJson, uint32_t handle);

#endif // AMXXRPC_BOTMETHODS_H

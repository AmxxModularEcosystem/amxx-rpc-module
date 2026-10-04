#ifndef AMXXRPC_EVENTS_H
#define AMXXRPC_EVENTS_H

// Event subscriptions and push delivery (design/12 §7). The subscription map is
// owned by the main thread: both subscribe/unsubscribe (builtin methods) and
// emit (metamod hooks / ARpc_Core_Emit) run on the main thread, so no locking.

#include <cstdint>
#include <string>

void Events_Init();
void Events_Shutdown();

// Subscribe/unsubscribe a session to a named event. Returns false on empty name.
bool Events_Subscribe(uint64_t sessionId, const std::string& event);
bool Events_Unsubscribe(uint64_t sessionId, const std::string& event);

// Emits an event to all subscribers as a JSON-RPC notification. payloadJson is
// a JSON value text (validated by the caller). Dead sessions are pruned lazily.
void Events_Emit(const std::string& event, const std::string& payloadJson);

// Drops subscriptions whose session is no longer authenticated/alive.
void Events_Prune();

#endif // AMXXRPC_EVENTS_H

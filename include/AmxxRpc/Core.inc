#if defined _amxxrpc_core_included
	#endinput
#endif
#define _amxxrpc_core_included

#define ARP_VERSION "0.1.0"

#define ARP_ERR_INVALID_PARAMS      -32602
#define ARP_ERR_INTERNAL            -32603
#define ARP_ERR_SERVICE_UNAVAILABLE -32002
#define ARP_ERR_ENGINE              -32003

// Registers a Pawn method. `callback` is a public function of the plugin with
// the signature: callback(const requestId, const paramsJson[]).
native bool:ARpc_Core_RegisterMethod(const name[], const callback[], const description[] = "");
native bool:ARpc_Core_UnregisterMethod(const name[]);

// Replies to a request dispatched to this plugin. Exactly one reply per request.
native ARpc_Core_Reply(const requestId, const resultJson[]);
native ARpc_Core_ReplyError(const requestId, const errorCode, const message[]);

// Emits a named event to subscribed sessions.
native ARpc_Core_Emit(const event[], const payloadJson[]);

// True when at least one authenticated client is connected.
native ARpc_Core_IsConnected();

// Writes the module version into `out`; returns the length.
native ARpc_Core_GetVersion(out[], len);

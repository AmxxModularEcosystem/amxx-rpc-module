#include "CoreMethods.h"

#include "amxxmodule.h"

#include "Events.h"
#include "Protocol.h"
#include "RpcDispatch.h"
#include "RpcRegistry.h"

#include "parson.h"

#include <string>

// Defined in the vendored SDK (amxxmodule.cpp); not declared in the header.
extern globalvars_t* gpGlobals;

namespace {

bool GetStringParam(const std::string& paramsJson, const char* key, std::string& out) {
	if (paramsJson.empty())
		return false;
	JSON_Value* root = json_parse_string(paramsJson.c_str());
	if (!root)
		return false;
	bool ok = false;
	if (json_value_get_type(root) == JSONObject) {
		const char* value = json_object_get_string(json_value_get_object(root), key);
		if (value) {
			out = value;
			ok = true;
		}
	}
	json_value_free(root);
	return ok;
}

bool GetNumberParam(const std::string& paramsJson, const char* key, double& out) {
	if (paramsJson.empty())
		return false;
	JSON_Value* root = json_parse_string(paramsJson.c_str());
	if (!root)
		return false;
	bool ok = false;
	if (json_value_get_type(root) == JSONObject) {
		JSON_Value* value = json_object_get_value(json_value_get_object(root), key);
		if (value && json_value_get_type(value) == JSONNumber) {
			out = json_value_get_number(value);
			ok = true;
		}
	}
	json_value_free(root);
	return ok;
}

void ReplyJson(uint32_t handle, JSON_Value* value) {
	if (!value) {
		Rpc_ReplyError(handle, RPC_INTERNAL_ERROR, "internal error");
		return;
	}
	char* serialized = json_serialize_to_string(value);
	std::string out = serialized ? serialized : "null";
	if (serialized)
		json_free_serialized_string(serialized);
	json_value_free(value);
	Rpc_Reply(handle, out);
}

JSON_Value* BuildPlayerValue(int index) {
	JSON_Value* objValue = json_value_init_object();
	if (!objValue)
		return nullptr;
	JSON_Object* obj = json_value_get_object(objValue);
	edict_t* edict = MF_GetPlayerEdict(index);
	const char* name = MF_GetPlayerName(index);
	const char* team = MF_GetPlayerTeam(index);
	const char* authid = edict ? g_engfuncs.pfnGetPlayerAuthId(edict) : nullptr;

	json_object_set_number(obj, "index", (double)index);
	json_object_set_string(obj, "name", name ? name : "");
	json_object_set_string(obj, "authid", authid ? authid : "");
	json_object_set_string(obj, "team", team ? team : "");
	json_object_set_number(obj, "health", (double)MF_GetPlayerHealth(index));

	JSON_Value* originValue = json_value_init_array();
	JSON_Array* origin = json_value_get_array(originValue);
	entvars_t* pev = edict ? g_engfuncs.pfnGetVarsOfEnt(edict) : nullptr;
	if (pev) {
		json_array_append_number(origin, (double)pev->origin[0]);
		json_array_append_number(origin, (double)pev->origin[1]);
		json_array_append_number(origin, (double)pev->origin[2]);
	}
	json_object_set_value(obj, "origin", originValue);
	return objValue;
}

void HandleServerExec(const std::string& paramsJson, uint32_t handle) {
	std::string command;
	if (!GetStringParam(paramsJson, "command", command) || command.empty()) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing command");
		return;
	}
	g_engfuncs.pfnServerCommand(command.c_str());
	g_engfuncs.pfnServerExecute();
	Rpc_Reply(handle, "{\"ok\":true}");
}

void HandleCvarGet(const std::string& paramsJson, uint32_t handle) {
	std::string name;
	if (!GetStringParam(paramsJson, "name", name) || name.empty()) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing name");
		return;
	}
	const char* value = g_engfuncs.pfnCVarGetString(name.c_str());
	JSON_Value* objValue = json_value_init_object();
	JSON_Object* obj = json_value_get_object(objValue);
	json_object_set_string(obj, "name", name.c_str());
	json_object_set_string(obj, "value", value ? value : "");
	ReplyJson(handle, objValue);
}

void HandleCvarSet(const std::string& paramsJson, uint32_t handle) {
	std::string name;
	std::string value;
	if (!GetStringParam(paramsJson, "name", name) || name.empty()) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing name");
		return;
	}
	if (!GetStringParam(paramsJson, "value", value)) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing value");
		return;
	}
	g_engfuncs.pfnCVarSetString(name.c_str(), value.c_str());
	JSON_Value* objValue = json_value_init_object();
	JSON_Object* obj = json_value_get_object(objValue);
	json_object_set_string(obj, "name", name.c_str());
	json_object_set_string(obj, "value", value.c_str());
	ReplyJson(handle, objValue);
}

void HandlePlayersList(uint32_t handle) {
	JSON_Value* arrValue = json_value_init_array();
	JSON_Array* arr = json_value_get_array(arrValue);
	int maxClients = gpGlobals ? gpGlobals->maxClients : 32;
	for (int i = 1; i <= maxClients; ++i) {
		if (!MF_IsPlayerValid(i) || !MF_GetPlayerEdict(i))
			continue;
		JSON_Value* player = BuildPlayerValue(i);
		if (player)
			json_array_append_value(arr, player);
	}
	ReplyJson(handle, arrValue);
}

void HandlePlayersGet(const std::string& paramsJson, uint32_t handle) {
	double index = 0;
	if (!GetNumberParam(paramsJson, "index", index)) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing index");
		return;
	}
	int i = (int)index;
	if (i < 1 || !MF_IsPlayerValid(i) || !MF_GetPlayerEdict(i)) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "invalid player index");
		return;
	}
	ReplyJson(handle, BuildPlayerValue(i));
}

void HandleSubscribe(const std::string& paramsJson, uint32_t handle, uint64_t sessionId) {
	std::string event;
	if (!GetStringParam(paramsJson, "event", event) || event.empty()) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing event");
		return;
	}
	Events_Subscribe(sessionId, event);
	Rpc_Reply(handle, "{\"ok\":true}");
}

void HandleUnsubscribe(const std::string& paramsJson, uint32_t handle, uint64_t sessionId) {
	std::string event;
	if (!GetStringParam(paramsJson, "event", event) || event.empty()) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing event");
		return;
	}
	Events_Unsubscribe(sessionId, event);
	Rpc_Reply(handle, "{\"ok\":true}");
}

} // namespace

void Core_Init() {
	RpcRegistry* registry = Rpc_GetRegistry();
	if (!registry)
		return;
	registry->AddTransport("rpc.auth", "session authentication");
	registry->AddBuiltin("rpc.ping", "liveness check");
	registry->AddBuiltin("rpc.version", "module and protocol version");
	registry->AddBuiltin("rpc.methods", "registered method catalog");
	registry->AddBuiltin("server.exec", "execute a server command");
	registry->AddBuiltin("cvar.get", "read a cvar");
	registry->AddBuiltin("cvar.set", "write a cvar");
	registry->AddBuiltin("players.list", "list connected players");
	registry->AddBuiltin("players.get", "get one player");
	registry->AddBuiltin("events.subscribe", "subscribe to an event");
	registry->AddBuiltin("events.unsubscribe", "unsubscribe from an event");
}

void Core_Shutdown() {
}

bool Core_Dispatch(const std::string& method, const std::string& paramsJson,
                   uint32_t handle, uint64_t sessionId) {
	if (method == "rpc.ping") {
		Rpc_Reply(handle, "{\"pong\":true}");
	} else if (method == "rpc.version") {
		Rpc_Reply(handle, "{\"module\":\"" MODULE_VERSION "\",\"protocol\":\"" ARP_PROTO_VERSION "\"}");
	} else if (method == "rpc.methods") {
		RpcRegistry* registry = Rpc_GetRegistry();
		Rpc_Reply(handle, registry ? registry->CatalogJson() : "[]");
	} else if (method == "server.exec") {
		HandleServerExec(paramsJson, handle);
	} else if (method == "cvar.get") {
		HandleCvarGet(paramsJson, handle);
	} else if (method == "cvar.set") {
		HandleCvarSet(paramsJson, handle);
	} else if (method == "players.list") {
		HandlePlayersList(handle);
	} else if (method == "players.get") {
		HandlePlayersGet(paramsJson, handle);
	} else if (method == "events.subscribe") {
		HandleSubscribe(paramsJson, handle, sessionId);
	} else if (method == "events.unsubscribe") {
		HandleUnsubscribe(paramsJson, handle, sessionId);
	} else {
		return false;
	}
	return true;
}

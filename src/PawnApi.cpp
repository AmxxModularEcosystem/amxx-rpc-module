#include "PawnApi.h"

#include "amxxmodule.h"

#include "Events.h"
#include "RpcDispatch.h"
#include "RpcRegistry.h"
#include "Transport.h"

#include "parson.h"

#include <cstring>
#include <string>

namespace {

bool IsValidJson(const char* text) {
	if (!text)
		return false;
	JSON_Value* value = json_parse_string(text);
	if (!value)
		return false;
	json_value_free(value);
	return true;
}

// AMXX implementation of the registry's forward registrar (design/12 §4).
class AmxxForwardRegistrar : public IRpcForwardRegistrar {
public:
	int RegisterForward(int pluginId, const std::string& callback) {
		AMX* amx = MF_GetScriptAmx(pluginId);
		if (!amx)
			return -1;
		return MF_RegisterSPForwardByName(amx, callback.c_str(), FP_CELL, FP_STRING, FP_DONE);
	}

	void UnregisterForward(int forwardId) {
		if (forwardId >= 0)
			MF_UnregisterSPForward(forwardId);
	}
};

AmxxForwardRegistrar g_registrar;

cell AMX_NATIVE_CALL Native_RegisterMethod(AMX* amx, cell* params) {
	int argc = (int)(params[0] / sizeof(cell));
	int len = 0;
	const char* name = MF_GetAmxString(amx, params[1], 0, &len);
	const char* callback = MF_GetAmxString(amx, params[2], 1, &len);
	std::string description;
	if (argc >= 3) {
		const char* desc = MF_GetAmxString(amx, params[3], 2, &len);
		if (desc)
			description = desc;
	}
	int pluginId = MF_FindScriptByAmx(amx);
	RpcRegistry* registry = Rpc_GetRegistry();
	if (!registry || !name || !callback || pluginId < 0)
		return 0;
	return registry->AddPawn(name, callback, description, pluginId) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_UnregisterMethod(AMX* amx, cell* params) {
	int len = 0;
	const char* name = MF_GetAmxString(amx, params[1], 0, &len);
	int pluginId = MF_FindScriptByAmx(amx);
	RpcRegistry* registry = Rpc_GetRegistry();
	if (!registry || !name || pluginId < 0)
		return 0;
	return registry->RemovePawn(name, pluginId) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_Reply(AMX* amx, cell* params) {
	uint32_t handle = (uint32_t)params[1];
	int len = 0;
	const char* resultJson = MF_GetAmxString(amx, params[2], 0, &len);
	int pluginId = MF_FindScriptByAmx(amx);
	if (!resultJson || pluginId < 0)
		return 0;
	if (!IsValidJson(resultJson)) {
		Rpc_ReplyErrorFromPlugin(handle, pluginId, RPC_INTERNAL_ERROR, "invalid result JSON");
		return 0;
	}
	return Rpc_ReplyFromPlugin(handle, pluginId, resultJson) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_ReplyError(AMX* amx, cell* params) {
	uint32_t handle = (uint32_t)params[1];
	int code = (int)params[2];
	int len = 0;
	const char* message = MF_GetAmxString(amx, params[3], 0, &len);
	int pluginId = MF_FindScriptByAmx(amx);
	if (!message || pluginId < 0)
		return 0;
	return Rpc_ReplyErrorFromPlugin(handle, pluginId, code, message) ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_Emit(AMX* amx, cell* params) {
	int len = 0;
	const char* event = MF_GetAmxString(amx, params[1], 0, &len);
	const char* payloadJson = MF_GetAmxString(amx, params[2], 1, &len);
	if (!event || !payloadJson)
		return 0;
	if (!IsValidJson(payloadJson))
		return 0;
	Events_Emit(event, payloadJson);
	return 1;
}

cell AMX_NATIVE_CALL Native_IsConnected(AMX* amx, cell* params) {
	(void)amx;
	(void)params;
	return Transport_AuthenticatedCount() > 0 ? 1 : 0;
}

cell AMX_NATIVE_CALL Native_GetVersion(AMX* amx, cell* params) {
	int max = (int)params[2];
	return (cell)MF_SetAmxString(amx, params[1], MODULE_VERSION, max);
}

AMX_NATIVE_INFO g_natives[] = {
	{"ARpc_Core_RegisterMethod", Native_RegisterMethod},
	{"ARpc_Core_UnregisterMethod", Native_UnregisterMethod},
	{"ARpc_Core_Reply", Native_Reply},
	{"ARpc_Core_ReplyError", Native_ReplyError},
	{"ARpc_Core_Emit", Native_Emit},
	{"ARpc_Core_IsConnected", Native_IsConnected},
	{"ARpc_Core_GetVersion", Native_GetVersion},
	{nullptr, nullptr},
};

} // namespace

void Pawn_Init() {
	RpcRegistry* registry = Rpc_GetRegistry();
	if (registry)
		registry->SetRegistrar(&g_registrar);
	MF_AddNatives(g_natives);
}

void Pawn_Shutdown() {
	RpcRegistry* registry = Rpc_GetRegistry();
	if (registry)
		registry->RemoveAllPawn();
}

void Pawn_OnPluginsUnloading() {
	RpcRegistry* registry = Rpc_GetRegistry();
	if (registry)
		registry->RemoveAllPawn();
	Rpc_FailPawnRequests(RPC_SERVICE_UNAVAILABLE, "plugin unloaded");
}

void Pawn_Dispatch(const RpcMethodInfo& info, const RpcRequest& req, uint32_t handle) {
	// Liveness: never execute a forward whose owner plugin is gone (design/12 §8).
	AMX* amx = MF_GetScriptAmx(info.pluginId);
	if (!amx || MF_FindScriptByAmx(amx) != info.pluginId) {
		RpcRegistry* registry = Rpc_GetRegistry();
		if (registry)
			registry->RemovePawn(info.name, info.pluginId);
		Rpc_ReplyError(handle, RPC_SERVICE_UNAVAILABLE, "method unavailable");
		return;
	}
	// Record ownership before executing: the callback may reply synchronously.
	Rpc_SetOutstandingPlugin(handle, info.pluginId);
	const std::string paramsJson = req.paramsJson.empty() ? std::string("null") : req.paramsJson;
	int rc = MF_ExecuteForward(info.forwardId, (cell)handle, paramsJson.c_str());
	if (rc < 0) {
		RpcRegistry* registry = Rpc_GetRegistry();
		if (registry)
			registry->RemovePawn(info.name, info.pluginId);
		Rpc_ReplyError(handle, RPC_SERVICE_UNAVAILABLE, "method unavailable");
	}
}

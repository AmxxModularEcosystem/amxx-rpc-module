#include "amxxmodule.h"

#include "Bot.h"
#include "Config.h"
#include "CoreMethods.h"
#include "Events.h"
#include "Fake.h"
#include "Log.h"
#include "PawnApi.h"
#include "Protocol.h"
#include "Queue.h"
#include "RpcDispatch.h"
#include "Transport.h"

#include "parson.h"

#include <memory>
#include <mutex>
#include <string>

extern globalvars_t* gpGlobals;

namespace {

std::unique_ptr<Queue<RpcRequest> > g_inbox;
std::unique_ptr<Queue<RpcOut> > g_outbox;
Config g_config;
bool g_initialized = false;
std::mutex g_lifecycleMutex;

std::string BuildPath(const char* localInfoKey, const char* fallback, const char* file) {
	const char* dir = MF_GetLocalInfo(localInfoKey, fallback);
	char buffer[512];
	MF_BuildPathnameR(buffer, sizeof(buffer), "%s/%s", dir ? dir : fallback, file);
	return std::string(buffer);
}

// Default YAPB core path (design/14 §9 M10); used only for the optional
// self-load. Detection itself is by module basename.
std::string YapbPath() {
	if (!g_config.yapbPath.empty())
		return g_config.yapbPath;
	char buffer[512];
#if defined(_WIN32)
	MF_BuildPathnameR(buffer, sizeof(buffer), "addons/yapb/bin/yapb.dll");
#else
	MF_BuildPathnameR(buffer, sizeof(buffer), "addons/yapb/bin/yapb.so");
#endif
	return std::string(buffer);
}

void Shutdown() {
	std::lock_guard<std::mutex> lock(g_lifecycleMutex);
	if (!g_initialized)
		return;
	Transport_Stop();
	Pawn_Shutdown();
	Events_Shutdown();
	Fake_Shutdown();
	Bot_Shutdown();
	Rpc_Shutdown();
	Log_Write(ARP_LOG_INFO, "shutdown complete");
	Log_Shutdown();
	g_initialized = false;
}

void StartTransport() {
	std::string error;
	if (!Config_Load(BuildPath("amx_configsdir", "addons/amxmodx/configs", "amxxrpc.cfg"),
	                 g_config, error)) {
		Log_Write(ARP_LOG_ERROR, "config error: %s", error.c_str());
		return;
	}
	Log_SetLevel(g_config.logLevel);
	g_inbox.reset(new Queue<RpcRequest>((size_t)g_config.inboxDepth));
	size_t outboxCapacity = (size_t)g_config.outboxDepth * (size_t)g_config.maxConnections;
	g_outbox.reset(new Queue<RpcOut>(outboxCapacity));
	Rpc_Init(g_config, g_inbox.get(), g_outbox.get());
	if (!Transport_Start(g_config, g_inbox.get(), g_outbox.get()))
		Log_Write(ARP_LOG_ERROR, "transport failed to start");
}

void CmdStatus() {
	MF_Log("[AmxxRpc] status: %s", Transport_StatusLine().c_str());
	MF_Log("[AmxxRpc] yapb: %s", Bot_Available() ? "available" : "not available");
}

void CmdClients() {
	MF_Log("[AmxxRpc] clients: %d", Transport_ClientCount());
}

void CmdHelp() {
	MF_Log("[AmxxRpc] commands: amxxrpc_status, amxxrpc_clients, amxxrpc_reload, amxxrpc_help");
}

void CmdReload() {
	std::lock_guard<std::mutex> lock(g_lifecycleMutex);
	Config newConfig;
	std::string error;
	if (!Config_Load(BuildPath("amx_configsdir", "addons/amxmodx/configs", "amxxrpc.cfg"),
	                 newConfig, error)) {
		MF_Log("[AmxxRpc] reload failed: %s", error.c_str());
		return;
	}
	if (Config_RestartKeysChanged(g_config, newConfig)) {
		bool hostPortChanged = g_config.host != newConfig.host || g_config.port != newConfig.port;
		if (hostPortChanged) {
			if (!Transport_Restart(newConfig)) {
				MF_Log("[AmxxRpc] reload: transport restart failed; keeping old transport");
				return;
			}
		} else {
			if (g_config.token != newConfig.token)
				Transport_ApplyToken(newConfig.token);
			Transport_ApplyLive(newConfig);
			Rpc_ApplyLive(newConfig);
		}
	} else {
		Transport_ApplyLive(newConfig);
		Rpc_ApplyLive(newConfig);
	}
	Log_SetLevel(newConfig.logLevel);
	g_config = newConfig;
	MF_Log("[AmxxRpc] reloaded");
}

void RegisterCommands() {
	g_engfuncs.pfnAddServerCommand("amxxrpc_status", CmdStatus);
	g_engfuncs.pfnAddServerCommand("amxxrpc_clients", CmdClients);
	g_engfuncs.pfnAddServerCommand("amxxrpc_reload", CmdReload);
	g_engfuncs.pfnAddServerCommand("amxxrpc_help", CmdHelp);
}

} // namespace

void OnAmxxAttach() {
	MF_Log("%s v%s loaded.", MODULE_NAME, MODULE_VERSION);
	Log_Init(BuildPath("amx_logsdir", "addons/amxmodx/logs", "amxxrpc.log"), ARP_LOG_INFO);
	StartTransport();
	Core_Init();
	Fake_Init(g_config.fakeMax);
	Bot_Init(YapbPath(), g_config.yapbSelfLoad);
	if (!Bot_Available())
		Log_Write(ARP_LOG_INFO, "YAPB not available; bot.* disabled");
	Pawn_Init();
	Events_Init();
	RegisterCommands();
	g_initialized = true;
}

void OnAmxxDetach() {
	Shutdown();
}

void OnMetaDetach() {
	Shutdown();
}

void FN_StartFrame_Post() {
	if (!g_initialized)
		return;
	Rpc_Tick();
	Fake_Think();
	std::string line;
	int budget = 32;
	while (budget-- > 0 && Log_PopLine(line))
		MF_Log("%s", line.c_str());
}

namespace {

void EmitJson(const char* event, JSON_Value* value) {
	if (!value)
		return;
	char* serialized = json_serialize_to_string(value);
	Events_Emit(event, serialized ? serialized : "{}");
	if (serialized)
		json_free_serialized_string(serialized);
	json_value_free(value);
}

} // namespace

BOOL ClientConnect(edict_t* pEntity, const char* pszName, const char* pszAddress,
                   char szRejectReason[128]) {
	(void)pEntity;
	(void)szRejectReason;
	if (g_initialized) {
		JSON_Value* obj = json_value_init_object();
		JSON_Object* o = json_value_get_object(obj);
		json_object_set_string(o, "name", pszName ? pszName : "");
		json_object_set_string(o, "address", pszAddress ? pszAddress : "");
		EmitJson("player_connect", obj);
	}
	return TRUE;
}

void ClientDisconnect(edict_t* pEntity) {
	if (!g_initialized)
		return;
	int index = pEntity ? g_engfuncs.pfnIndexOfEdict(pEntity) : 0;
	const char* name = index > 0 ? MF_GetPlayerName(index) : nullptr;
	JSON_Value* obj = json_value_init_object();
	JSON_Object* o = json_value_get_object(obj);
	json_object_set_number(o, "index", (double)index);
	json_object_set_string(o, "name", name ? name : "");
	EmitJson("player_disconnect", obj);
}

void ServerActivate(edict_t* pEdictList, int edictCount, int clientMax) {
	(void)pEdictList;
	(void)edictCount;
	(void)clientMax;
	if (!g_initialized)
		return;
	Fake_OnMapStart();
	Bot_OnMapStart();
	const char* map = gpGlobals ? g_engfuncs.pfnSzFromIndex(gpGlobals->mapname) : nullptr;
	JSON_Value* obj = json_value_init_object();
	json_object_set_string(json_value_get_object(obj), "map", map ? map : "");
	EmitJson("map_start", obj);
}

void OnPluginsUnloading() {
	Pawn_OnPluginsUnloading();
}

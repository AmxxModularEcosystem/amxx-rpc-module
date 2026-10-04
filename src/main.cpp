#include "amxxmodule.h"

#include "Config.h"
#include "Log.h"
#include "Protocol.h"
#include "Queue.h"
#include "RpcDispatch.h"
#include "Transport.h"

#include <memory>
#include <mutex>
#include <string>

namespace {

std::unique_ptr<Queue<RpcRequest> > g_inbox;
std::unique_ptr<Queue<RpcOut> > g_outbox;
Config g_config;
bool g_initialized = false;
std::mutex g_lifecycleMutex;

std::string BuildPath(const char* localInfoKey, const char* fallback, const char* file) {
	const char* dir = MF_GetLocalInfo(localInfoKey, fallback);
	std::string path = dir ? dir : fallback;
	if (!path.empty() && path.back() != '/' && path.back() != '\\')
		path += "/";
	path += file;
	return path;
}

void Shutdown() {
	std::lock_guard<std::mutex> lock(g_lifecycleMutex);
	if (!g_initialized)
		return;
	Transport_Stop();
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
	std::string line;
	int budget = 32;
	while (budget-- > 0 && Log_PopLine(line))
		MF_Log("%s", line.c_str());
}

#include <amxmodx>
#include <AmxxRpc/Core>

public stock const PluginName[]    = "AmxxRpc Example";
public stock const PluginVersion[] = ARP_VERSION;
public stock const PluginAuthor[]  = "AmxxRpc";

public plugin_precache()
{
	ARpc_Core_RegisterMethod("demo.echo", "DemoEcho_Handler", "Echoes the request params back");
}

public DemoEcho_Handler(const requestId, const paramsJson[])
{
	ARpc_Core_Reply(requestId, "{^"ok^":true}");
}

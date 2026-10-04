#ifndef __MODULECONFIG_H__
#define __MODULECONFIG_H__

#define MODULE_NAME "AmxxRpc"
#define MODULE_VERSION "0.1.3"
#define MODULE_AUTHOR "AmxxRpc"
#define MODULE_URL "https://example.invalid/amxxrpc"
#define MODULE_LOGTAG "AmxxRpc"
#define MODULE_LIBRARY ""
#define MODULE_LIBCLASS ""

// Keep loaded across map changes so the transport/session survives (revisited in a later stage).
// #define MODULE_RELOAD_ON_MAPCHANGE

#ifdef __DATE__
#define MODULE_DATE __DATE__
#else
#define MODULE_DATE "Unknown"
#endif

// Metamod hooks are required for StartFrame / GetPlayerAuthId / CreateFakeClient (AD-7).
#define USE_METAMOD

#define FN_AMXX_ATTACH OnAmxxAttach
#define FN_AMXX_DETACH OnAmxxDetach

// Stage 1: drain the I/O->main queue every frame (design/11 §6).
#define FN_StartFrame_Post StartFrame_Post

// Stage 1: idempotent shutdown also on Metamod detach (design/11 §6).
#define FN_META_DETACH OnMetaDetach

// Stage 2: event sources (design/12 §7) and Pawn cleanup on global unload (§8).
#define FN_ClientConnect ClientConnect
#define FN_ClientDisconnect ClientDisconnect
#define FN_ServerActivate ServerActivate
#define FN_AMXX_PLUGINSUNLOADING OnPluginsUnloading

// Stage 3: authid substitution for fake players (design/13 §4, AR-015).
#define FN_GetPlayerAuthId GetPlayerAuthId

#endif // __MODULECONFIG_H__

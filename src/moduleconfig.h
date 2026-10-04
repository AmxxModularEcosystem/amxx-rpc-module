#ifndef __MODULECONFIG_H__
#define __MODULECONFIG_H__

#define MODULE_NAME "AmxxRpc"
#define MODULE_VERSION "0.0.1"
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

#endif // __MODULECONFIG_H__

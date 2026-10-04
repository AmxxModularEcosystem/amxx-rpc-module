#ifndef AMXXRPC_FAKE_H
#define AMXXRPC_FAKE_H

// Engine-level fake players (design/13, docs/04). Every function here runs on
// the main thread only: the I/O thread never touches this module (AR-002).
//
// Creation order is critical (design/13 §3): the engine's pfnCreateFakeClient
// sets FL_FAKECLIENT and calls ClientUserInfoChanged, where AMXX registers the
// bot and reads GETPLAYERAUTHID. The authid therefore has to be available
// *before* the call, via a module-level pending buffer consumed by the
// FN_GetPlayerAuthId pre-hook.

#include "amxxmodule.h"

#include "Protocol.h"

#include <string>
#include <vector>

struct FakeRecord {
	int entIndex = 0;
	edict_t* ent = nullptr;
	std::string authid;   // substituted identity; buffer must outlive the hook
	std::string name;
	bool alive = false;   // spawned and not dead
	float viewAngles[3] = {0.0f, 0.0f, 0.0f};
	float fwd = 0.0f;
	float side = 0.0f;
	float up = 0.0f;
	unsigned short buttons = 0; // held buttons (incl. IN_ATTACK)
	unsigned char impulse = 0;
	bool pendingRemove = false;
};

struct FakeCreateResult {
	bool ok = false;
	int index = 0;
	std::string authid;
	int errorCode = RPC_INTERNAL_ERROR;
	std::string error;
};

// Sets the fake limit (config fake_max). Does not touch existing records.
void Fake_Init(int fakeMax);
void Fake_SetMax(int fakeMax);

// Drops all records (no engine calls; used on module shutdown).
void Fake_Shutdown();

// Creates a fake client. Empty authid => a unique generated one.
FakeCreateResult Fake_Create(const std::string& name, const std::string& authid);

// Removes a registered fake: MDLL_ClientDisconnect + edict removal + record drop.
bool Fake_Remove(int index, int& errorCode, std::string& error);

// Per-frame usercmd drive for alive fakes (main thread, StartFrame).
void Fake_Think();

// Recomputes the alive flag for every record (death/respawn).
void Fake_RefreshAlive();

// Map change: edicts may be invalidated, so records are dropped.
void Fake_OnMapStart();

FakeRecord* Fake_GetRecord(int index);
bool Fake_IsFake(int index);
int Fake_Count();
std::vector<int> Fake_Indices();

#endif // AMXXRPC_FAKE_H

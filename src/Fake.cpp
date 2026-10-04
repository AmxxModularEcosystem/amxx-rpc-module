#include "Fake.h"

#include "FakeMethods.h"
#include "Log.h"

#include <map>
#include <string>

// Defined in the vendored SDK (amxxmodule.cpp); not declared in the header.
extern globalvars_t* gpGlobals;

namespace {

// Main-thread registry: entIndex -> record. std::map keeps authid buffers
// stable across inserts (node-based), which the authid hook relies on.
std::map<int, FakeRecord> g_fakes;
int g_fakeMax = 8;
std::string g_pendingAuthid; // set just before pfnCreateFakeClient, cleared after
unsigned int g_authidSeq = 0;

bool IsAliveEnt(edict_t* ent) {
	if (!ent)
		return false;
	entvars_t* pev = &ent->v;
	return pev->health > 0.0f && pev->deadflag == DEAD_NO;
}

} // namespace

void Fake_Init(int fakeMax) {
	Fake_SetMax(fakeMax);
	g_pendingAuthid.clear();
	FakeMethods_Init();
}

void Fake_SetMax(int fakeMax) {
	g_fakeMax = fakeMax < 0 ? 0 : fakeMax;
}

void Fake_Shutdown() {
	g_fakes.clear();
	g_pendingAuthid.clear();
}

FakeCreateResult Fake_Create(const std::string& name, const std::string& authid) {
	FakeCreateResult result;
	if (name.empty()) {
		result.errorCode = RPC_INVALID_PARAMS;
		result.error = "name must not be empty";
		return result;
	}
	if (static_cast<int>(g_fakes.size()) >= g_fakeMax) {
		result.errorCode = RPC_SERVER_BUSY;
		result.error = "fake_max reached";
		return result;
	}

	std::string effectiveAuthid = authid;
	if (effectiveAuthid.empty())
		effectiveAuthid = "ARPC_" + std::to_string(++g_authidSeq);

	// The engine reads the authid *inside* pfnCreateFakeClient (via
	// ClientUserInfoChanged -> AMXX), so it must be pending before the call.
	g_pendingAuthid = effectiveAuthid;
	edict_t* ent = g_engfuncs.pfnCreateFakeClient(name.c_str());
	g_pendingAuthid.clear();

	if (!ent) {
		result.errorCode = RPC_ENGINE_ERROR;
		result.error = "pfnCreateFakeClient returned null";
		return result;
	}

	int entIndex = g_engfuncs.pfnIndexOfEdict(ent);
	if (entIndex < 1) {
		g_engfuncs.pfnRemoveEntity(ent);
		result.errorCode = RPC_ENGINE_ERROR;
		result.error = "invalid fake client index";
		return result;
	}

	FakeRecord record;
	record.entIndex = entIndex;
	record.ent = ent;
	record.authid = effectiveAuthid;
	record.name = name;
	record.alive = false;
	g_fakes[entIndex] = record;

	char reject[128] = {0};
	if (!MDLL_ClientConnect(ent, name.c_str(), "127.0.0.1", reject)) {
		g_fakes.erase(entIndex);
		g_engfuncs.pfnRemoveEntity(ent);
		result.errorCode = RPC_ENGINE_ERROR;
		result.error = reject[0] ? reject : "ClientConnect rejected";
		return result;
	}
	MDLL_ClientPutInServer(ent);

	// The engine already sets these, but keep them explicit (design/13 §3).
	ent->v.flags |= (FL_FAKECLIENT | FL_CLIENT);

	if (!IsAliveEnt(ent))
		MDLL_Spawn(ent);

	FakeRecord* stored = Fake_GetRecord(entIndex);
	if (stored)
		stored->alive = IsAliveEnt(ent);

	result.ok = true;
	result.index = entIndex;
	result.authid = effectiveAuthid;
	return result;
}

bool Fake_Remove(int index, int& errorCode, std::string& error) {
	std::map<int, FakeRecord>::iterator it = g_fakes.find(index);
	if (it == g_fakes.end()) {
		errorCode = RPC_INVALID_PARAMS;
		error = "index is not a registered fake";
		return false;
	}
	it->second.pendingRemove = true;
	edict_t* ent = it->second.ent;
	if (ent) {
		MDLL_ClientDisconnect(ent);
		g_engfuncs.pfnRemoveEntity(ent);
	}
	g_fakes.erase(it);
	return true;
}

void Fake_Think() {
	if (g_fakes.empty())
		return;

	Fake_RefreshAlive();

	float frametime = gpGlobals ? gpGlobals->frametime : 0.0f;
	if (frametime <= 0.0f)
		frametime = 0.01f;
	int msec = static_cast<int>(frametime * 1000.0f + 0.5f);
	if (msec < 1)
		msec = 1;
	if (msec > 255)
		msec = 255;

	for (std::map<int, FakeRecord>::iterator it = g_fakes.begin(); it != g_fakes.end(); ++it) {
		FakeRecord& rec = it->second;
		if (rec.pendingRemove || !rec.ent || !rec.alive)
			continue; // never drive dead/not-spawned fakes (design/13 §5)
		g_engfuncs.pfnRunPlayerMove(rec.ent, rec.viewAngles, rec.fwd, rec.side, rec.up,
		                            rec.buttons, static_cast<byte>(rec.impulse),
		                            static_cast<byte>(msec));
		rec.impulse = 0; // impulse is one-shot
	}
}

void Fake_RefreshAlive() {
	for (std::map<int, FakeRecord>::iterator it = g_fakes.begin(); it != g_fakes.end(); ++it) {
		if (it->second.ent)
			it->second.alive = IsAliveEnt(it->second.ent);
	}
}

void Fake_OnMapStart() {
	// Edicts may be invalidated by the map change; drop records to avoid
	// dangling pointers. Fakes must be recreated after a map change.
	g_fakes.clear();
}

FakeRecord* Fake_GetRecord(int index) {
	std::map<int, FakeRecord>::iterator it = g_fakes.find(index);
	return it == g_fakes.end() ? nullptr : &it->second;
}

bool Fake_IsFake(int index) {
	return g_fakes.find(index) != g_fakes.end();
}

int Fake_Count() {
	return static_cast<int>(g_fakes.size());
}

std::vector<int> Fake_Indices() {
	std::vector<int> indices;
	indices.reserve(g_fakes.size());
	for (std::map<int, FakeRecord>::const_iterator it = g_fakes.begin(); it != g_fakes.end(); ++it)
		indices.push_back(it->first);
	return indices;
}

// Metamod pre-hook (moduleconfig.h: FN_GetPlayerAuthId). Returns the substituted
// authid for registered fakes and for the fake currently being created (pending
// buffer + FL_FAKECLIENT); otherwise defers to the engine.
const char* GetPlayerAuthId(edict_t* e) {
	if (e) {
		int index = g_engfuncs.pfnIndexOfEdict(e);
		std::map<int, FakeRecord>::iterator it = g_fakes.find(index);
		if (it != g_fakes.end())
			RETURN_META_VALUE(MRES_SUPERCEDE, it->second.authid.c_str());

		if (!g_pendingAuthid.empty()) {
			entvars_t* pev = g_engfuncs.pfnGetVarsOfEnt(e);
			if (pev && (pev->flags & FL_FAKECLIENT))
				RETURN_META_VALUE(MRES_SUPERCEDE, g_pendingAuthid.c_str());
		}
	}
	RETURN_META_VALUE(MRES_IGNORED, NULL);
}

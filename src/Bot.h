#ifndef AMXXRPC_BOT_H
#define AMXXRPC_BOT_H

// Optional YAPB adapter (design/14). YAPB is never required: when the core is
// not loaded in-process, Bot_Available() is false and bot.* degrade to -32002.
// All calls run on the main thread (AR-002). The IBotModule* is never cached
// across a map change (YAPB reloads on mapchange, design/14 §9 B1) and is never
// deleted (design/14 §9 M9).

#include <string>

// Thin seam over the subset of bot::IBotModule the adapter uses. Tests inject a
// mock; production wraps the real interface (design/14 §9 M6).
class IBotApi {
public:
	virtual ~IBotApi() {}
	virtual const char* GetBotVersion() = 0;
	virtual bool IsBotsInGame() = 0;
	virtual bool IsBot(int entity) = 0;
	virtual int GetBotCount() = 0;
	virtual bool AddBot(const char* name, int difficulty, int personality, int team) = 0;
	virtual void SetBotGoal(int entity, int node) = 0;
	virtual void SetBotGoalOrigin(int entity, float* origin) = 0;
	virtual void SetBotLookAt(int entity, float* origin) = 0;
	virtual void SetBotMovement(int entity, bool move) = 0;
	virtual float* GetBotOrigin(int entity) = 0;
	virtual int GetBotEnemy(int entity) = 0;
	virtual int GetBotWeapon(int entity) = 0;
	virtual int GetBotTask(int entity) = 0;
};

struct BotResult {
	bool ok = false;
	bool queued = false;
	int errorCode = 0;
	std::string error;
};

struct BotStatus {
	int index = 0;
	float origin[3] = {0.0f, 0.0f, 0.0f};
	bool hasOrigin = false;
	int enemy = 0;
	int weapon = 0;
	int task = 0;
};

// Lifecycle (main thread). yapbPath is used only for the optional self-load;
// detection itself is by module basename. selfLoad is config-gated and off by
// default (design/14 §9 B2).
void Bot_Init(const std::string& yapbPath, bool selfLoad);
void Bot_Shutdown();
void Bot_OnMapStart();

// Availability: YAPB core loaded in-process AND GetBotAPI resolved.
bool Bot_Available();
std::string Bot_Version();

// Low-level wrappers (call only when Bot_Available()).
bool Bot_IsBotsInGame();
bool Bot_IsBot(int entity);
int Bot_GetBotCount();
bool Bot_AddBot(const char* name, int difficulty, int personality, int team);
void Bot_SetBotGoal(int entity, int node);
void Bot_SetBotGoalOrigin(int entity, float* origin);
void Bot_SetBotLookAt(int entity, float* origin);
void Bot_SetBotMovement(int entity, bool move);
float* Bot_GetBotOrigin(int entity);
int Bot_GetBotEnemy(int entity);
int Bot_GetBotWeapon(int entity);
int Bot_GetBotTask(int entity);

// Guarded operations used by BotMethods: availability + param + IsBot checks
// mapped to JSON-RPC error codes. Testable off-line.
BotResult Bot_Add(const std::string& name, int difficulty, int personality, int team);
BotResult Bot_Goal(int index, const float* origin, int node, bool useNode);
BotResult Bot_Look(int index, const float* origin);
BotResult Bot_Freeze(int index, bool frozen);
BotResult Bot_Status(int index, BotStatus& out);

// Test seam: inject a mock API (marks the adapter available). The mock is not
// owned by the adapter.
void Bot_SetApiForTest(IBotApi* api);
void Bot_ClearApiForTest();

#endif // AMXXRPC_BOT_H

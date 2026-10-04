// Off-line unit tests for the optional YAPB adapter (no AMXX SDK, no YAPB).
// A mock IBotApi is injected through the thin seam to exercise degradation,
// parameter validation, the IsBot guard, and JSON-RPC error mapping.
// Build: g++ -m32 -std=c++17 -Isrc -Ithird_party/yapb tests/bot_test.cpp \
//        src/Bot.cpp -o /tmp/bot_test

#include "Bot.h"

#include "BotMethods.h"
#include "Protocol.h"

#include <cstdio>
#include <set>
#include <string>

// Bot_Init registers the RPC methods; the off-line test has no registry, so the
// registration entry point is stubbed out.
void BotMethods_Init() {}

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                          \
	do {                                                                     \
		++g_checks;                                                          \
		if (!(cond)) {                                                       \
			++g_failures;                                                    \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
		}                                                                    \
	} while (0)

#define CHECK_EQ(a, b)                                                       \
	do {                                                                     \
		++g_checks;                                                          \
		if (!((a) == (b))) {                                                 \
			++g_failures;                                                    \
			std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); \
		}                                                                    \
	} while (0)

class MockBotApi : public IBotApi {
public:
	std::string version = "3.0.0";
	bool botsInGame = false;
	int botCount = 0;
	bool addResult = true;
	std::set<int> bots;

	int lastGoalEntity = -1;
	int lastGoalNode = -1;
	bool goalOriginCalled = false;
	float lastGoalOrigin[3] = {0.0f, 0.0f, 0.0f};
	int lastLookEntity = -1;
	float lastLookOrigin[3] = {0.0f, 0.0f, 0.0f};
	int lastMoveEntity = -1;
	bool lastMove = false;
	float origin[3] = {10.0f, 20.0f, 30.0f};
	int enemy = 0;
	int weapon = 0;
	int task = 0;

	const char* GetBotVersion() override { return version.c_str(); }
	bool IsBotsInGame() override { return botsInGame; }
	bool IsBot(int entity) override { return bots.count(entity) > 0; }
	int GetBotCount() override { return botCount; }
	bool AddBot(const char* name, int difficulty, int personality, int team) override {
		(void)name;
		(void)difficulty;
		(void)personality;
		(void)team;
		return addResult;
	}
	void SetBotGoal(int entity, int node) override {
		lastGoalEntity = entity;
		lastGoalNode = node;
	}
	void SetBotGoalOrigin(int entity, float* o) override {
		lastGoalEntity = entity;
		goalOriginCalled = true;
		lastGoalOrigin[0] = o[0];
		lastGoalOrigin[1] = o[1];
		lastGoalOrigin[2] = o[2];
	}
	void SetBotLookAt(int entity, float* o) override {
		lastLookEntity = entity;
		lastLookOrigin[0] = o[0];
		lastLookOrigin[1] = o[1];
		lastLookOrigin[2] = o[2];
	}
	void SetBotMovement(int entity, bool move) override {
		lastMoveEntity = entity;
		lastMove = move;
	}
	float* GetBotOrigin(int entity) override {
		(void)entity;
		return origin;
	}
	int GetBotEnemy(int entity) override {
		(void)entity;
		return enemy;
	}
	int GetBotWeapon(int entity) override {
		(void)entity;
		return weapon;
	}
	int GetBotTask(int entity) override {
		(void)entity;
		return task;
	}
};

static void TestDegradation() {
	Bot_ClearApiForTest();
	CHECK(!Bot_Available());
	CHECK(Bot_Version().empty());

	BotResult add = Bot_Add("Bot", 1, 0, 0);
	CHECK(!add.ok);
	CHECK_EQ(add.errorCode, RPC_SERVICE_UNAVAILABLE);

	float origin[3] = {1.0f, 2.0f, 3.0f};
	CHECK_EQ(Bot_Goal(1, origin, 0, false).errorCode, RPC_SERVICE_UNAVAILABLE);
	CHECK_EQ(Bot_Look(1, origin).errorCode, RPC_SERVICE_UNAVAILABLE);
	CHECK_EQ(Bot_Freeze(1, true).errorCode, RPC_SERVICE_UNAVAILABLE);
	BotStatus status;
	CHECK_EQ(Bot_Status(1, status).errorCode, RPC_SERVICE_UNAVAILABLE);

	// Low-level wrappers are safe no-ops without an API.
	CHECK(!Bot_IsBotsInGame());
	CHECK(!Bot_IsBot(1));
	CHECK_EQ(Bot_GetBotCount(), 0);
	CHECK(Bot_GetBotOrigin(1) == nullptr);
}

static void TestAvailableAndAdd() {
	MockBotApi mock;
	Bot_SetApiForTest(&mock);
	CHECK(Bot_Available());
	CHECK_EQ(Bot_Version(), std::string("3.0.0"));

	BotResult add = Bot_Add("Bot", 1, 0, 0);
	CHECK(add.ok);
	CHECK(add.queued);

	BotResult empty = Bot_Add("", 1, 0, 0);
	CHECK(!empty.ok);
	CHECK_EQ(empty.errorCode, RPC_INVALID_PARAMS);

	mock.addResult = false;
	BotResult notQueued = Bot_Add("Bot", 1, 0, 0);
	CHECK(notQueued.ok);
	CHECK(!notQueued.queued);
}

static void TestIsBotGuard() {
	MockBotApi mock;
	mock.bots.insert(3);
	Bot_SetApiForTest(&mock);

	float origin[3] = {1.0f, 2.0f, 3.0f};
	// index 5 is a real player, not a YAPB bot -> -32602.
	CHECK_EQ(Bot_Goal(5, origin, 0, false).errorCode, RPC_INVALID_PARAMS);
	CHECK_EQ(Bot_Look(5, origin).errorCode, RPC_INVALID_PARAMS);
	CHECK_EQ(Bot_Freeze(5, true).errorCode, RPC_INVALID_PARAMS);
	BotStatus status;
	CHECK_EQ(Bot_Status(5, status).errorCode, RPC_INVALID_PARAMS);
}

static void TestParamValidation() {
	MockBotApi mock;
	mock.bots.insert(3);
	Bot_SetApiForTest(&mock);

	float origin[3] = {1.0f, 2.0f, 3.0f};
	CHECK_EQ(Bot_Goal(0, origin, 0, false).errorCode, RPC_INVALID_PARAMS);
	CHECK_EQ(Bot_Goal(3, nullptr, 0, false).errorCode, RPC_INVALID_PARAMS);
	CHECK_EQ(Bot_Look(0, origin).errorCode, RPC_INVALID_PARAMS);
	CHECK_EQ(Bot_Look(3, nullptr).errorCode, RPC_INVALID_PARAMS);
	CHECK_EQ(Bot_Freeze(0, true).errorCode, RPC_INVALID_PARAMS);
	BotStatus status;
	CHECK_EQ(Bot_Status(0, status).errorCode, RPC_INVALID_PARAMS);
}

static void TestSuccessPaths() {
	MockBotApi mock;
	mock.bots.insert(3);
	mock.enemy = 7;
	mock.weapon = 42;
	mock.task = 9;
	Bot_SetApiForTest(&mock);

	float origin[3] = {1.5f, 2.5f, 3.5f};
	BotResult goal = Bot_Goal(3, origin, 0, false);
	CHECK(goal.ok);
	CHECK(mock.goalOriginCalled);
	CHECK_EQ(mock.lastGoalEntity, 3);
	CHECK_EQ(mock.lastGoalOrigin[0], 1.5f);
	CHECK_EQ(mock.lastGoalOrigin[1], 2.5f);
	CHECK_EQ(mock.lastGoalOrigin[2], 3.5f);

	BotResult nodeGoal = Bot_Goal(3, nullptr, 11, true);
	CHECK(nodeGoal.ok);
	CHECK_EQ(mock.lastGoalEntity, 3);
	CHECK_EQ(mock.lastGoalNode, 11);

	BotResult look = Bot_Look(3, origin);
	CHECK(look.ok);
	CHECK_EQ(mock.lastLookEntity, 3);
	CHECK_EQ(mock.lastLookOrigin[0], 1.5f);

	BotResult freeze = Bot_Freeze(3, true);
	CHECK(freeze.ok);
	CHECK_EQ(mock.lastMoveEntity, 3);
	CHECK(!mock.lastMove); // frozen => movement disabled

	BotResult unfreeze = Bot_Freeze(3, false);
	CHECK(unfreeze.ok);
	CHECK(mock.lastMove);

	BotStatus status;
	BotResult statusResult = Bot_Status(3, status);
	CHECK(statusResult.ok);
	CHECK_EQ(status.index, 3);
	CHECK(status.hasOrigin);
	CHECK_EQ(status.origin[0], 10.0f);
	CHECK_EQ(status.origin[1], 20.0f);
	CHECK_EQ(status.origin[2], 30.0f);
	CHECK_EQ(status.enemy, 7);
	CHECK_EQ(status.weapon, 42);
	CHECK_EQ(status.task, 9);
}

static void TestWrappers() {
	MockBotApi mock;
	mock.botsInGame = true;
	mock.botCount = 4;
	mock.bots.insert(2);
	Bot_SetApiForTest(&mock);

	CHECK(Bot_IsBotsInGame());
	CHECK_EQ(Bot_GetBotCount(), 4);
	CHECK(Bot_IsBot(2));
	CHECK(!Bot_IsBot(1));
}

static void TestMapStartResets() {
	MockBotApi mock;
	Bot_SetApiForTest(&mock);
	CHECK(Bot_Available());
	Bot_OnMapStart();
	// The borrowed pointer is dropped; without a resident core we degrade.
	CHECK(!Bot_Available());
}

int main() {
	TestDegradation();
	TestAvailableAndAdd();
	TestIsBotGuard();
	TestParamValidation();
	TestSuccessPaths();
	TestWrappers();
	TestMapStartResets();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

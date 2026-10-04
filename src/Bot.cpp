#include "Bot.h"

#include "BotMethods.h"
#include "Protocol.h"

#include "module.h"

#include <cstring>
#include <string>

#if defined(_WIN32)
	#include <windows.h>
#else
	#include <dlfcn.h>
	#include <link.h>
#endif

// Optional YAPB adapter (design/14). Detection is by module basename: the YAPB
// core (yapb.{dll,so}) is loaded by the yapb_amxx AMXX module, not by us. A
// non-null GetBotAPI is NOT a readiness signal (design/14 §9 B2), so availability
// requires the core to be resident in-process first. Self-load is off by default
// and gated behind config; loading the core as a plain library does not register
// the engine and can hit uninitialized globals.

namespace {

IBotApi* g_api = nullptr;
bool g_apiOwned = false;   // true when g_api is our adapter allocation
bool g_selfLoaded = false; // true when we loaded the YAPB library ourselves
std::string g_yapbPath;
bool g_selfLoad = false;

#if defined(_WIN32)
typedef bot::IBotModule* (*GetBotApiFn)(int);
HMODULE g_libraryHandle = nullptr;
#else
typedef bot::IBotModule* (*GetBotApiFn)(int);
void* g_libraryHandle = nullptr;
#endif

// Adapter: wraps the real IBotModule behind the thin IBotApi seam. The module
// pointer is borrowed; the adapter never deletes it (design/14 §9 M9).
class BotModuleApi : public IBotApi {
public:
	explicit BotModuleApi(bot::IBotModule* module) : m_module(module) {}

	const char* GetBotVersion() override { return m_module->GetBotVersion(); }
	bool IsBotsInGame() override { return m_module->IsBotsInGame(); }
	bool IsBot(int entity) override { return m_module->IsBot(entity); }
	int GetBotCount() override { return m_module->GetBotCount(); }
	bool AddBot(const char* name, int difficulty, int personality, int team) override {
		return m_module->AddBot(name, difficulty, personality, team);
	}
	void SetBotGoal(int entity, int node) override { m_module->SetBotGoal(entity, node); }
	void SetBotGoalOrigin(int entity, float* origin) override {
		m_module->SetBotGoalOrigin(entity, origin);
	}
	void SetBotLookAt(int entity, float* origin) override { m_module->SetBotLookAt(entity, origin); }
	void SetBotMovement(int entity, bool move) override { m_module->SetBotMovement(entity, move); }
	float* GetBotOrigin(int entity) override { return m_module->GetBotOrigin(entity); }
	int GetBotEnemy(int entity) override { return m_module->GetBotEnemy(entity); }
	int GetBotWeapon(int entity) override { return m_module->GetBotWeapon(entity); }
	int GetBotTask(int entity) override { return m_module->GetBotTask(entity); }

private:
	bot::IBotModule* m_module;
};

#if defined(_WIN32)

// The YAPB core is loaded by yapb_amxx; detect the already-resident module.
IBotApi* ResolveFromLoaded() {
	HMODULE handle = GetModuleHandleA("yapb.dll");
	if (!handle)
		return nullptr;
	GetBotApiFn fn = reinterpret_cast<GetBotApiFn>(GetProcAddress(handle, "GetBotAPI"));
	if (!fn)
		return nullptr;
	bot::IBotModule* module = fn(bot::kBotModuleVersion);
	if (!module)
		return nullptr;
	return new BotModuleApi(module);
}

bool SelfLoadLibrary(const std::string& path) {
	if (path.empty())
		return false;
	HMODULE handle = LoadLibraryA(path.c_str());
	if (!handle)
		return false;
	GetBotApiFn fn = reinterpret_cast<GetBotApiFn>(GetProcAddress(handle, "GetBotAPI"));
	if (!fn) {
		FreeLibrary(handle);
		return false;
	}
	bot::IBotModule* module = fn(bot::kBotModuleVersion);
	if (!module) {
		FreeLibrary(handle);
		return false;
	}
	g_libraryHandle = handle;
	g_api = new BotModuleApi(module);
	g_apiOwned = true;
	g_selfLoaded = true;
	return true;
}

void UnloadSelfLoaded() {
	if (g_selfLoaded && g_libraryHandle) {
		FreeLibrary(g_libraryHandle);
		g_libraryHandle = nullptr;
	}
	g_selfLoaded = false;
}

#else // !_WIN32

struct FindCtx {
	const char* base;
	char path[4096];
	bool found;
};

int FindCallback(struct dl_phdr_info* info, size_t size, void* data) {
	(void)size;
	FindCtx* ctx = static_cast<FindCtx*>(data);
	if (!info->dlpi_name || !info->dlpi_name[0])
		return 0;
	const char* slash = std::strrchr(info->dlpi_name, '/');
	const char* base = slash ? slash + 1 : info->dlpi_name;
	if (std::strcmp(base, ctx->base) == 0) {
		std::strncpy(ctx->path, info->dlpi_name, sizeof(ctx->path) - 1);
		ctx->path[sizeof(ctx->path) - 1] = '\0';
		ctx->found = true;
		return 1;
	}
	return 0;
}

// Linux has no SONAME for yapb.so, so RTLD_NOLOAD by name fails; locate the
// resident module by basename via dl_iterate_phdr, then open it by full path.
bool FindYapbModule(std::string& path) {
	FindCtx ctx;
	ctx.base = "yapb.so";
	ctx.path[0] = '\0';
	ctx.found = false;
	dl_iterate_phdr(FindCallback, &ctx);
	if (!ctx.found)
		return false;
	path = ctx.path;
	return true;
}

IBotApi* ResolveFromLoaded() {
	std::string path;
	if (!FindYapbModule(path))
		return nullptr;
	void* handle = dlopen(path.c_str(), RTLD_NOLOAD | RTLD_NOW);
	if (!handle)
		return nullptr;
	GetBotApiFn fn = reinterpret_cast<GetBotApiFn>(dlsym(handle, "GetBotAPI"));
	if (!fn) {
		dlclose(handle);
		return nullptr;
	}
	bot::IBotModule* module = fn(bot::kBotModuleVersion);
	dlclose(handle); // yapb_amxx keeps its own reference; the pointer stays valid
	if (!module)
		return nullptr;
	return new BotModuleApi(module);
}

bool SelfLoadLibrary(const std::string& path) {
	if (path.empty())
		return false;
	void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!handle)
		return false;
	GetBotApiFn fn = reinterpret_cast<GetBotApiFn>(dlsym(handle, "GetBotAPI"));
	if (!fn) {
		dlclose(handle);
		return false;
	}
	bot::IBotModule* module = fn(bot::kBotModuleVersion);
	if (!module) {
		dlclose(handle);
		return false;
	}
	g_libraryHandle = handle;
	g_api = new BotModuleApi(module);
	g_apiOwned = true;
	g_selfLoaded = true;
	return true;
}

void UnloadSelfLoaded() {
	if (g_selfLoaded && g_libraryHandle) {
		dlclose(g_libraryHandle);
		g_libraryHandle = nullptr;
	}
	g_selfLoaded = false;
}

#endif

void ResetApi() {
	if (g_apiOwned && g_api)
		delete g_api;
	g_api = nullptr;
	g_apiOwned = false;
}

// Lazily (re)resolve the already-loaded core. Never self-loads here.
void EnsureResolved() {
	if (g_api)
		return;
	g_api = ResolveFromLoaded();
	g_apiOwned = (g_api != nullptr);
}

} // namespace

void Bot_Init(const std::string& yapbPath, bool selfLoad) {
	g_yapbPath = yapbPath;
	g_selfLoad = selfLoad;
	ResetApi();
	EnsureResolved();
	if (!g_api && g_selfLoad)
		SelfLoadLibrary(g_yapbPath);
	BotMethods_Init();
}

void Bot_Shutdown() {
	ResetApi();
	UnloadSelfLoaded();
	g_yapbPath.clear();
	g_selfLoad = false;
}

void Bot_OnMapStart() {
	// YAPB reloads on mapchange and unloads its core; drop the borrowed pointer
	// and re-resolve lazily (design/14 §9 B1).
	ResetApi();
}

bool Bot_Available() {
	EnsureResolved();
	return g_api != nullptr;
}

std::string Bot_Version() {
	EnsureResolved();
	if (!g_api)
		return std::string();
	const char* version = g_api->GetBotVersion();
	return version ? std::string(version) : std::string();
}

bool Bot_IsBotsInGame() { return g_api ? g_api->IsBotsInGame() : false; }
bool Bot_IsBot(int entity) { return g_api ? g_api->IsBot(entity) : false; }
int Bot_GetBotCount() { return g_api ? g_api->GetBotCount() : 0; }

bool Bot_AddBot(const char* name, int difficulty, int personality, int team) {
	return g_api ? g_api->AddBot(name, difficulty, personality, team) : false;
}

void Bot_SetBotGoal(int entity, int node) {
	if (g_api)
		g_api->SetBotGoal(entity, node);
}

void Bot_SetBotGoalOrigin(int entity, float* origin) {
	if (g_api)
		g_api->SetBotGoalOrigin(entity, origin);
}

void Bot_SetBotLookAt(int entity, float* origin) {
	if (g_api)
		g_api->SetBotLookAt(entity, origin);
}

void Bot_SetBotMovement(int entity, bool move) {
	if (g_api)
		g_api->SetBotMovement(entity, move);
}

float* Bot_GetBotOrigin(int entity) { return g_api ? g_api->GetBotOrigin(entity) : nullptr; }
int Bot_GetBotEnemy(int entity) { return g_api ? g_api->GetBotEnemy(entity) : 0; }
int Bot_GetBotWeapon(int entity) { return g_api ? g_api->GetBotWeapon(entity) : 0; }
int Bot_GetBotTask(int entity) { return g_api ? g_api->GetBotTask(entity) : 0; }

BotResult Bot_Add(const std::string& name, int difficulty, int personality, int team) {
	BotResult result;
	if (!Bot_Available()) {
		result.errorCode = RPC_SERVICE_UNAVAILABLE;
		result.error = "YAPB not available";
		return result;
	}
	if (name.empty()) {
		result.errorCode = RPC_INVALID_PARAMS;
		result.error = "name must not be empty";
		return result;
	}
	result.queued = g_api->AddBot(name.c_str(), difficulty, personality, team);
	result.ok = true;
	return result;
}

BotResult Bot_Goal(int index, const float* origin, int node, bool useNode) {
	BotResult result;
	if (!Bot_Available()) {
		result.errorCode = RPC_SERVICE_UNAVAILABLE;
		result.error = "YAPB not available";
		return result;
	}
	if (index < 1) {
		result.errorCode = RPC_INVALID_PARAMS;
		result.error = "invalid index";
		return result;
	}
	if (!useNode && !origin) {
		result.errorCode = RPC_INVALID_PARAMS;
		result.error = "missing origin or node";
		return result;
	}
	if (!g_api->IsBot(index)) {
		result.errorCode = RPC_INVALID_PARAMS;
		result.error = "index is not a YAPB bot";
		return result;
	}
	if (useNode) {
		g_api->SetBotGoal(index, node);
	} else {
		float copy[3] = {origin[0], origin[1], origin[2]};
		g_api->SetBotGoalOrigin(index, copy);
	}
	result.ok = true;
	return result;
}

BotResult Bot_Look(int index, const float* origin) {
	BotResult result;
	if (!Bot_Available()) {
		result.errorCode = RPC_SERVICE_UNAVAILABLE;
		result.error = "YAPB not available";
		return result;
	}
	if (index < 1) {
		result.errorCode = RPC_INVALID_PARAMS;
		result.error = "invalid index";
		return result;
	}
	if (!origin) {
		result.errorCode = RPC_INVALID_PARAMS;
		result.error = "missing origin";
		return result;
	}
	if (!g_api->IsBot(index)) {
		result.errorCode = RPC_INVALID_PARAMS;
		result.error = "index is not a YAPB bot";
		return result;
	}
	float copy[3] = {origin[0], origin[1], origin[2]};
	g_api->SetBotLookAt(index, copy);
	result.ok = true;
	return result;
}

BotResult Bot_Freeze(int index, bool frozen) {
	BotResult result;
	if (!Bot_Available()) {
		result.errorCode = RPC_SERVICE_UNAVAILABLE;
		result.error = "YAPB not available";
		return result;
	}
	if (index < 1) {
		result.errorCode = RPC_INVALID_PARAMS;
		result.error = "invalid index";
		return result;
	}
	if (!g_api->IsBot(index)) {
		result.errorCode = RPC_INVALID_PARAMS;
		result.error = "index is not a YAPB bot";
		return result;
	}
	g_api->SetBotMovement(index, !frozen);
	result.ok = true;
	return result;
}

BotResult Bot_Status(int index, BotStatus& out) {
	BotResult result;
	if (!Bot_Available()) {
		result.errorCode = RPC_SERVICE_UNAVAILABLE;
		result.error = "YAPB not available";
		return result;
	}
	if (index < 1) {
		result.errorCode = RPC_INVALID_PARAMS;
		result.error = "invalid index";
		return result;
	}
	if (!g_api->IsBot(index)) {
		result.errorCode = RPC_INVALID_PARAMS;
		result.error = "index is not a YAPB bot";
		return result;
	}
	out.index = index;
	float* origin = g_api->GetBotOrigin(index);
	if (origin) {
		out.origin[0] = origin[0];
		out.origin[1] = origin[1];
		out.origin[2] = origin[2];
		out.hasOrigin = true;
	}
	out.enemy = g_api->GetBotEnemy(index);
	out.weapon = g_api->GetBotWeapon(index);
	out.task = g_api->GetBotTask(index);
	result.ok = true;
	return result;
}

void Bot_SetApiForTest(IBotApi* api) {
	ResetApi();
	g_api = api;
	g_apiOwned = false;
}

void Bot_ClearApiForTest() {
	ResetApi();
}

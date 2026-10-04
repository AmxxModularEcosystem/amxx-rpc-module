// Off-line unit tests for the RPC method registry (no AMXX SDK).
// Build: g++ -m32 -std=c++17 -Isrc -Ithird_party/parson tests/registry_test.cpp \
//        src/RpcRegistry.cpp /tmp/parson.o -o /tmp/registry_test

#include "RpcRegistry.h"

#include "parson.h"

#include <cstdio>
#include <string>
#include <vector>

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

class FakeRegistrar : public IRpcForwardRegistrar {
public:
	int nextId = 100;
	int failForPlugin = -1;
	std::string lastCallback;
	std::vector<int> unregistered;

	int RegisterForward(int pluginId, const std::string& callback) {
		if (pluginId == failForPlugin)
			return -1;
		lastCallback = callback;
		return nextId++;
	}

	void UnregisterForward(int forwardId) {
		unregistered.push_back(forwardId);
	}
};

static const char* SourceOf(const RpcRegistry& registry, const std::string& name) {
	const RpcMethodInfo* info = registry.Find(name);
	if (!info)
		return "<missing>";
	switch (info->source) {
		case RPC_SOURCE_PAWN:      return "pawn";
		case RPC_SOURCE_TRANSPORT: return "transport";
		case RPC_SOURCE_BUILTIN:
		default:                   return "builtin";
	}
}

static void TestBuiltinAndTransport() {
	RpcRegistry registry;
	CHECK(registry.AddBuiltin("rpc.ping", "liveness"));
	CHECK(registry.AddTransport("rpc.auth", "auth"));
	CHECK_EQ(registry.Size(), (size_t)2);
	CHECK_EQ(std::string(SourceOf(registry, "rpc.ping")), std::string("builtin"));
	CHECK_EQ(std::string(SourceOf(registry, "rpc.auth")), std::string("transport"));
	CHECK(!registry.AddBuiltin("rpc.ping", "dup"));
}

static void TestPawnRegistration() {
	FakeRegistrar registrar;
	RpcRegistry registry(&registrar);
	CHECK(registry.AddPawn("demo.echo", "DemoEcho_Handler", "echo", 7));
	const RpcMethodInfo* info = registry.Find("demo.echo");
	CHECK(info != nullptr);
	CHECK_EQ(info->pluginId, 7);
	CHECK_EQ(info->forwardId, 100);
	CHECK_EQ(registrar.lastCallback, std::string("DemoEcho_Handler"));
	CHECK_EQ(std::string(SourceOf(registry, "demo.echo")), std::string("pawn"));
}

static void TestDuplicatePawnRejected() {
	FakeRegistrar registrar;
	RpcRegistry registry(&registrar);
	CHECK(registry.AddPawn("demo.echo", "Handler", "", 1));
	CHECK(!registry.AddPawn("demo.echo", "Handler2", "", 2));
}

static void TestCoreOverrideForbidden() {
	FakeRegistrar registrar;
	RpcRegistry registry(&registrar);
	CHECK(registry.AddBuiltin("server.exec", "exec"));
	CHECK(registry.AddTransport("rpc.auth", "auth"));
	CHECK(!registry.AddPawn("server.exec", "Handler", "", 1));
	CHECK(!registry.AddPawn("rpc.auth", "Handler", "", 1));
	CHECK(!registry.AddPawn("rpc.ping", "Handler", "", 1));
	CHECK(registry.IsCoreName("rpc.anything"));
	CHECK(!registry.IsCoreName("demo.echo"));
}

static void TestForwardFailureRejected() {
	FakeRegistrar registrar;
	registrar.failForPlugin = 5;
	RpcRegistry registry(&registrar);
	CHECK(!registry.AddPawn("demo.echo", "Handler", "", 5));
	CHECK(registry.Find("demo.echo") == nullptr);
}

static void TestUnregisterOwnership() {
	FakeRegistrar registrar;
	RpcRegistry registry(&registrar);
	CHECK(registry.AddPawn("demo.echo", "Handler", "", 1));
	CHECK(!registry.RemovePawn("demo.echo", 2)); // wrong owner
	CHECK(registry.Find("demo.echo") != nullptr);
	CHECK(registry.RemovePawn("demo.echo", 1));
	CHECK(registry.Find("demo.echo") == nullptr);
	CHECK_EQ(registrar.unregistered.size(), (size_t)1);
	CHECK_EQ(registrar.unregistered[0], 100);
}

static void TestRemoveAllPawn() {
	FakeRegistrar registrar;
	RpcRegistry registry(&registrar);
	CHECK(registry.AddBuiltin("rpc.ping", "liveness"));
	CHECK(registry.AddPawn("demo.a", "A", "", 1));
	CHECK(registry.AddPawn("demo.b", "B", "", 2));
	CHECK_EQ(registry.RemoveAllPawn(), 2);
	CHECK_EQ(registrar.unregistered.size(), (size_t)2);
	CHECK(registry.Find("rpc.ping") != nullptr);
	CHECK(registry.Find("demo.a") == nullptr);
	CHECK(registry.Find("demo.b") == nullptr);
}

static void TestCatalogJson() {
	FakeRegistrar registrar;
	RpcRegistry registry(&registrar);
	CHECK(registry.AddBuiltin("rpc.ping", "liveness"));
	CHECK(registry.AddTransport("rpc.auth", "auth"));
	CHECK(registry.AddPawn("demo.echo", "Handler", "echo", 1));

	std::string json = registry.CatalogJson();
	JSON_Value* root = json_parse_string(json.c_str());
	CHECK(root != nullptr);
	CHECK_EQ(json_value_get_type(root), JSONArray);
	JSON_Array* arr = json_value_get_array(root);
	CHECK_EQ(json_array_get_count(arr), (size_t)3);

	bool sawPing = false;
	bool sawAuth = false;
	bool sawEcho = false;
	for (size_t i = 0; i < json_array_get_count(arr); ++i) {
		JSON_Object* obj = json_value_get_object(json_array_get_value(arr, i));
		const char* name = json_object_get_string(obj, "name");
		const char* source = json_object_get_string(obj, "source");
		const char* description = json_object_get_string(obj, "description");
		CHECK(name != nullptr);
		CHECK(source != nullptr);
		CHECK(description != nullptr);
		if (name && std::string(name) == "rpc.ping") {
			sawPing = true;
			CHECK_EQ(std::string(source), std::string("builtin"));
			CHECK_EQ(std::string(description), std::string("liveness"));
		}
		if (name && std::string(name) == "rpc.auth") {
			sawAuth = true;
			CHECK_EQ(std::string(source), std::string("transport"));
		}
		if (name && std::string(name) == "demo.echo") {
			sawEcho = true;
			CHECK_EQ(std::string(source), std::string("pawn"));
		}
	}
	CHECK(sawPing);
	CHECK(sawAuth);
	CHECK(sawEcho);
	json_value_free(root);
}

int main() {
	TestBuiltinAndTransport();
	TestPawnRegistration();
	TestDuplicatePawnRejected();
	TestCoreOverrideForbidden();
	TestForwardFailureRejected();
	TestUnregisterOwnership();
	TestRemoveAllPawn();
	TestCatalogJson();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

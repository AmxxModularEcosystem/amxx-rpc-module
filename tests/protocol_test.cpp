// Off-line unit tests for the JSON-RPC codec (no AMXX SDK).
// Build: g++ -m32 -std=c++17 -Isrc -Ithird_party/parson tests/protocol_test.cpp \
//        src/Protocol.cpp /tmp/parson.o -o /tmp/protocol_test

#include "Protocol.h"

#include <cstdio>
#include <string>

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

static const size_t kMaxBytes = 1048576;

static ProtocolParseResult Parse(const std::string& text) {
	return Protocol_ParseRequest(text, kMaxBytes, ARP_MAX_JSON_DEPTH);
}

static void TestValidRequestNumericId() {
	ProtocolParseResult r = Parse(
	    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"rpc.ping\",\"params\":{}}");
	CHECK(r.ok);
	CHECK(!r.request.isNotification);
	CHECK_EQ(r.request.method, std::string("rpc.ping"));
	CHECK_EQ(r.request.rawId, std::string("1"));
	CHECK_EQ(r.request.paramsJson, std::string("{}"));
}

static void TestValidRequestStringId() {
	ProtocolParseResult r = Parse(
	    "{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"rpc.version\"}");
	CHECK(r.ok);
	CHECK(!r.request.isNotification);
	CHECK_EQ(r.request.rawId, std::string("\"abc\""));
	CHECK(r.request.paramsJson.empty());
}

static void TestRawIdPreservedVerbatim() {
	ProtocolParseResult r = Parse(
	    "{\"jsonrpc\":\"2.0\",\"id\":1.0,\"method\":\"rpc.ping\"}");
	CHECK(r.ok);
	CHECK_EQ(r.request.rawId, std::string("1.0"));

	ProtocolParseResult r2 = Parse(
	    "{\"jsonrpc\":\"2.0\",\"id\":  -42 ,\"method\":\"rpc.ping\"}");
	CHECK(r2.ok);
	CHECK_EQ(r2.request.rawId, std::string("-42"));
}

static void TestNotificationNoId() {
	ProtocolParseResult r = Parse(
	    "{\"jsonrpc\":\"2.0\",\"method\":\"rpc.ping\"}");
	CHECK(r.ok);
	CHECK(r.request.isNotification);
	CHECK(r.request.rawId.empty());
}

static void TestNotificationNullId() {
	ProtocolParseResult r = Parse(
	    "{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"rpc.ping\"}");
	CHECK(r.ok);
	CHECK(r.request.isNotification);
}

static void TestParseError() {
	ProtocolParseResult r = Parse("{not json");
	CHECK(!r.ok);
	CHECK_EQ(r.errorCode, (int)RPC_PARSE_ERROR);
}

static void TestBatchRejected() {
	ProtocolParseResult r = Parse(
	    "[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"rpc.ping\"}]");
	CHECK(!r.ok);
	CHECK(r.isBatch);
	CHECK_EQ(r.errorCode, (int)RPC_INVALID_REQUEST);
}

static void TestMissingJsonrpc() {
	ProtocolParseResult r = Parse("{\"id\":1,\"method\":\"rpc.ping\"}");
	CHECK(!r.ok);
	CHECK_EQ(r.errorCode, (int)RPC_INVALID_REQUEST);
}

static void TestWrongJsonrpc() {
	ProtocolParseResult r = Parse(
	    "{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"rpc.ping\"}");
	CHECK(!r.ok);
	CHECK_EQ(r.errorCode, (int)RPC_INVALID_REQUEST);
}

static void TestMissingMethod() {
	ProtocolParseResult r = Parse("{\"jsonrpc\":\"2.0\",\"id\":1}");
	CHECK(!r.ok);
	CHECK_EQ(r.errorCode, (int)RPC_INVALID_REQUEST);
}

static void TestNonStringMethod() {
	ProtocolParseResult r = Parse(
	    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":42}");
	CHECK(!r.ok);
	CHECK_EQ(r.errorCode, (int)RPC_INVALID_REQUEST);
}

static void TestInvalidParams() {
	ProtocolParseResult r = Parse(
	    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"rpc.ping\",\"params\":42}");
	CHECK(!r.ok);
	CHECK_EQ(r.errorCode, (int)RPC_INVALID_PARAMS);
}

static void TestTopLevelNotObject() {
	ProtocolParseResult r = Parse("\"hello\"");
	CHECK(!r.ok);
	CHECK_EQ(r.errorCode, (int)RPC_INVALID_REQUEST);
}

static void TestDepthGuard() {
	std::string deep = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"rpc.ping\",\"params\":";
	for (int i = 0; i < 64; ++i)
		deep += "[";
	for (int i = 0; i < 64; ++i)
		deep += "]";
	deep += "}";
	ProtocolParseResult r = Parse(deep);
	CHECK(!r.ok);
	CHECK_EQ(r.errorCode, (int)RPC_INVALID_REQUEST);
}

static void TestSizeGuard() {
	std::string big = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"rpc.ping\",\"params\":\"";
	big += std::string(200, 'x');
	big += "\"}";
	ProtocolParseResult r = Protocol_ParseRequest(big, 64, ARP_MAX_JSON_DEPTH);
	CHECK(!r.ok);
	CHECK_EQ(r.errorCode, (int)RPC_INVALID_REQUEST);
}

static void TestParamsArrayPreserved() {
	ProtocolParseResult r = Parse(
	    "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"rpc.ping\",\"params\":[1,2,3]}");
	CHECK(r.ok);
	CHECK_EQ(r.request.paramsJson, std::string("[1,2,3]"));
}

static void TestBuildResult() {
	std::string out = Protocol_BuildResult("1", "{\"ok\":true}");
	CHECK_EQ(out, std::string("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"ok\":true}}"));
}

static void TestBuildResultNullId() {
	std::string out = Protocol_BuildResult("", "null");
	CHECK_EQ(out, std::string("{\"jsonrpc\":\"2.0\",\"id\":null,\"result\":null}"));
}

static void TestBuildError() {
	std::string out = Protocol_BuildError("\"x\"", RPC_METHOD_NOT_FOUND, "method not found");
	CHECK_EQ(out, std::string(
	    "{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"error\":{\"code\":-32601,"
	    "\"message\":\"method not found\"}}"));
}

static void TestBuildErrorEscapes() {
	std::string out = Protocol_BuildError("1", RPC_INTERNAL_ERROR, "a\"b\\c");
	CHECK(out.find("a\\\"b\\\\c") != std::string::npos);
}

static void TestBuildNotification() {
	std::string out = Protocol_BuildNotification("event.tick", "{\"n\":1}");
	CHECK_EQ(out, std::string(
	    "{\"jsonrpc\":\"2.0\",\"method\":\"event.tick\",\"params\":{\"n\":1}}"));
}

static void TestExtractRawId() {
	std::string rawId;
	bool hasId = false;
	CHECK(Protocol_ExtractRawId("{\"a\":1,\"id\":\"z\",\"b\":2}", rawId, hasId));
	CHECK(hasId);
	CHECK_EQ(rawId, std::string("\"z\""));

	CHECK(Protocol_ExtractRawId("{\"method\":\"x\"}", rawId, hasId));
	CHECK(!hasId);
}

static void TestExtractRawIdNested() {
	std::string rawId;
	bool hasId = false;
	CHECK(Protocol_ExtractRawId(
	    "{\"params\":{\"id\":\"inner\"},\"id\":99}", rawId, hasId));
	CHECK(hasId);
	CHECK_EQ(rawId, std::string("99"));
}

int main() {
	TestValidRequestNumericId();
	TestValidRequestStringId();
	TestRawIdPreservedVerbatim();
	TestNotificationNoId();
	TestNotificationNullId();
	TestParseError();
	TestBatchRejected();
	TestMissingJsonrpc();
	TestWrongJsonrpc();
	TestMissingMethod();
	TestNonStringMethod();
	TestInvalidParams();
	TestTopLevelNotObject();
	TestDepthGuard();
	TestSizeGuard();
	TestParamsArrayPreserved();
	TestBuildResult();
	TestBuildResultNullId();
	TestBuildError();
	TestBuildErrorEscapes();
	TestBuildNotification();
	TestExtractRawId();
	TestExtractRawIdNested();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}

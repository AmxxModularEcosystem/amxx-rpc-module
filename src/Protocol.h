#ifndef AMXXRPC_PROTOCOL_H
#define AMXXRPC_PROTOCOL_H

// JSON-RPC 2.0 codec (design/11 §2, §9). This translation unit MUST NOT include
// any AMXX SDK header so it stays unit-testable off-line (tests/protocol_test.cpp).

#include <cstddef>
#include <cstdint>
#include <string>

// JSON-RPC 2.0 error codes (docs/02 §4).
enum RpcErrorCode {
	RPC_PARSE_ERROR         = -32700,
	RPC_INVALID_REQUEST     = -32600,
	RPC_METHOD_NOT_FOUND    = -32601,
	RPC_INVALID_PARAMS      = -32602,
	RPC_INTERNAL_ERROR      = -32603,
	RPC_NOT_AUTHENTICATED   = -32001,
	RPC_SERVICE_UNAVAILABLE = -32002,
	RPC_ENGINE_ERROR        = -32003,
	RPC_SERVER_BUSY         = -32004,
	RPC_REQUEST_TIMEOUT     = -32005,
};

// Protocol version reported by rpc.version (OQ-11, design/11 §14).
#define ARP_PROTO_VERSION "1.0"

// Default nesting-depth guard (design/11 §2, M-6).
#define ARP_MAX_JSON_DEPTH 32

// A parsed JSON-RPC request/notification handed from the I/O thread to main.
struct RpcRequest {
	uint32_t handle = 0;      // assigned by the main-thread dispatcher
	uint64_t sessionId = 0;   // set by the I/O thread
	std::string method;
	std::string paramsJson;   // raw JSON text of "params" (object/array), or empty
	std::string rawId;        // raw id token verbatim (M-9); empty => notification
	bool isNotification = false;
};

struct ProtocolParseResult {
	bool ok = false;
	int errorCode = 0;        // RpcErrorCode when !ok
	std::string errorMessage;
	bool isBatch = false;     // top-level JSON array (rejected, OQ-6)
	RpcRequest request;
};

// Pre-parse guard: size + nesting depth, run BEFORE parson (M-6).
// Returns false and fills errorCode/errorMessage on violation.
bool Protocol_Guard(const std::string& text, size_t maxBytes, int maxDepth,
                    int& errorCode, std::string& errorMessage);

// Parse one JSON-RPC 2.0 request/notification line.
ProtocolParseResult Protocol_ParseRequest(const std::string& text,
                                          size_t maxBytes, int maxDepth);

// Extract the raw "id" token from a JSON object text, verbatim (M-9).
bool Protocol_ExtractRawId(const std::string& text, std::string& rawId, bool& hasId);

// Builders. rawId is echoed verbatim; pass "null" when the id is unknown.
std::string Protocol_BuildResult(const std::string& rawId, const std::string& resultJson);
std::string Protocol_BuildError(const std::string& rawId, int code, const std::string& message);
std::string Protocol_BuildNotification(const std::string& method, const std::string& paramsJson);

#endif // AMXXRPC_PROTOCOL_H

#include "Protocol.h"

#include "parson.h"

#include <cstring>

namespace {

const char* const kHexDigits = "0123456789abcdef";

void AppendEscaped(std::string& out, const std::string& s) {
	for (size_t i = 0; i < s.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(s[i]);
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20) {
					out += "\\u00";
					out += kHexDigits[(c >> 4) & 0x0F];
					out += kHexDigits[c & 0x0F];
				} else {
					out += static_cast<char>(c);
				}
		}
	}
}

void SkipWs(const std::string& s, size_t& i) {
	while (i < s.size()) {
		char c = s[i];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			++i;
		else
			break;
	}
}

// s[i] == '"'; returns index just past the closing quote, or npos.
size_t ScanString(const std::string& s, size_t i) {
	++i;
	while (i < s.size()) {
		char c = s[i];
		if (c == '\\') {
			i += 2;
			continue;
		}
		if (c == '"')
			return i + 1;
		++i;
	}
	return std::string::npos;
}

// Returns the index just past a complete JSON value starting at i, or npos.
size_t ScanValueEnd(const std::string& s, size_t i) {
	if (i >= s.size())
		return std::string::npos;
	char c = s[i];
	if (c == '"')
		return ScanString(s, i);
	if (c == '{' || c == '[') {
		int depth = 0;
		while (i < s.size()) {
			char ch = s[i];
			if (ch == '"') {
				size_t end = ScanString(s, i);
				if (end == std::string::npos)
					return std::string::npos;
				i = end;
				continue;
			}
			if (ch == '{' || ch == '[') {
				++depth;
			} else if (ch == '}' || ch == ']') {
				--depth;
				if (depth == 0)
					return i + 1;
			}
			++i;
		}
		return std::string::npos;
	}
	size_t start = i;
	while (i < s.size()) {
		char ch = s[i];
		if (ch == ',' || ch == '}' || ch == ']' || ch == ' ' || ch == '\t' ||
		    ch == '\n' || ch == '\r')
			break;
		++i;
	}
	return i > start ? i : std::string::npos;
}

// Single pass: max nesting depth + raw top-level "id" token.
bool ScanJson(const std::string& text, int maxDepth, int& maxSeen, bool& depthExceeded,
              std::string& rawId, bool& hasId) {
	size_t i = 0;
	int depth = 0;
	maxSeen = 0;
	depthExceeded = false;
	hasId = false;
	rawId.clear();
	bool expectKeyAtDepth1 = false;

	while (i < text.size()) {
		char c = text[i];
		if (c == '"') {
			size_t end = ScanString(text, i);
			if (end == std::string::npos)
				return false;
			if (depth == 1 && expectKeyAtDepth1) {
				std::string token = text.substr(i, end - i);
				if (token == "\"id\"") {
					size_t j = end;
					SkipWs(text, j);
					if (j < text.size() && text[j] == ':') {
						++j;
						SkipWs(text, j);
						size_t valueEnd = ScanValueEnd(text, j);
						if (valueEnd == std::string::npos)
							return false;
						rawId = text.substr(j, valueEnd - j);
						hasId = true;
						i = valueEnd;
						expectKeyAtDepth1 = false;
						continue;
					}
				}
				expectKeyAtDepth1 = false;
			}
			i = end;
			continue;
		}
		if (c == '{' || c == '[') {
			++depth;
			if (depth > maxSeen)
				maxSeen = depth;
			if (depth > maxDepth)
				depthExceeded = true;
			if (depth == 1 && c == '{')
				expectKeyAtDepth1 = true;
			++i;
			continue;
		}
		if (c == '}' || c == ']') {
			--depth;
			if (depth < 0)
				return false;
			++i;
			continue;
		}
		if (c == ',') {
			if (depth == 1)
				expectKeyAtDepth1 = true;
			++i;
			continue;
		}
		++i;
	}
	return true;
}

} // namespace

bool Protocol_Guard(const std::string& text, size_t maxBytes, int maxDepth,
                    int& errorCode, std::string& errorMessage) {
	if (text.size() > maxBytes) {
		errorCode = RPC_INVALID_REQUEST;
		errorMessage = "message too large";
		return false;
	}
	int maxSeen = 0;
	bool depthExceeded = false;
	std::string rawId;
	bool hasId = false;
	if (!ScanJson(text, maxDepth, maxSeen, depthExceeded, rawId, hasId)) {
		errorCode = RPC_PARSE_ERROR;
		errorMessage = "malformed JSON";
		return false;
	}
	if (depthExceeded) {
		errorCode = RPC_INVALID_REQUEST;
		errorMessage = "JSON nesting too deep";
		return false;
	}
	return true;
}

bool Protocol_ExtractRawId(const std::string& text, std::string& rawId, bool& hasId) {
	int maxSeen = 0;
	bool depthExceeded = false;
	return ScanJson(text, 1024, maxSeen, depthExceeded, rawId, hasId);
}

ProtocolParseResult Protocol_ParseRequest(const std::string& text,
                                          size_t maxBytes, int maxDepth) {
	ProtocolParseResult res;

	int code = 0;
	std::string message;
	if (!Protocol_Guard(text, maxBytes, maxDepth, code, message)) {
		res.errorCode = code;
		res.errorMessage = message;
		return res;
	}

	JSON_Value* root = json_parse_string(text.c_str());
	if (!root) {
		res.errorCode = RPC_PARSE_ERROR;
		res.errorMessage = "parse error";
		return res;
	}

	JSON_Value_Type type = json_value_get_type(root);
	if (type == JSONArray) {
		res.isBatch = true;
		res.errorCode = RPC_INVALID_REQUEST;
		res.errorMessage = "batch not supported";
		json_value_free(root);
		return res;
	}
	if (type != JSONObject) {
		res.errorCode = RPC_INVALID_REQUEST;
		res.errorMessage = "request must be an object";
		json_value_free(root);
		return res;
	}

	JSON_Object* obj = json_value_get_object(root);
	const char* jsonrpc = json_object_get_string(obj, "jsonrpc");
	if (!jsonrpc || std::strcmp(jsonrpc, "2.0") != 0) {
		res.errorCode = RPC_INVALID_REQUEST;
		res.errorMessage = "invalid jsonrpc version";
		json_value_free(root);
		return res;
	}

	const char* method = json_object_get_string(obj, "method");
	if (!method) {
		res.errorCode = RPC_INVALID_REQUEST;
		res.errorMessage = "method must be a string";
		json_value_free(root);
		return res;
	}

	JSON_Value* idValue = json_object_get_value(obj, "id");
	bool isNotification = (idValue == nullptr) ||
	                      (json_value_get_type(idValue) == JSONNull);

	std::string paramsJson;
	JSON_Value* paramsValue = json_object_get_value(obj, "params");
	if (paramsValue) {
		JSON_Value_Type paramsType = json_value_get_type(paramsValue);
		if (paramsType != JSONObject && paramsType != JSONArray) {
			res.errorCode = RPC_INVALID_PARAMS;
			res.errorMessage = "params must be object or array";
			json_value_free(root);
			return res;
		}
		char* serialized = json_serialize_to_string(paramsValue);
		if (serialized) {
			paramsJson = serialized;
			json_free_serialized_string(serialized);
		}
	}

	std::string rawId;
	bool hasId = false;
	Protocol_ExtractRawId(text, rawId, hasId);

	res.ok = true;
	res.request.method = method;
	res.request.paramsJson = paramsJson;
	res.request.isNotification = isNotification;
	if (!isNotification && hasId)
		res.request.rawId = rawId;

	json_value_free(root);
	return res;
}

std::string Protocol_BuildResult(const std::string& rawId, const std::string& resultJson) {
	std::string out;
	out.reserve(64 + resultJson.size());
	out += "{\"jsonrpc\":\"2.0\",\"id\":";
	out += rawId.empty() ? "null" : rawId;
	out += ",\"result\":";
	out += resultJson.empty() ? "null" : resultJson;
	out += "}";
	return out;
}

std::string Protocol_BuildError(const std::string& rawId, int code, const std::string& message) {
	std::string out;
	out.reserve(96 + message.size());
	out += "{\"jsonrpc\":\"2.0\",\"id\":";
	out += rawId.empty() ? "null" : rawId;
	out += ",\"error\":{\"code\":";
	out += std::to_string(code);
	out += ",\"message\":\"";
	AppendEscaped(out, message);
	out += "\"}}";
	return out;
}

std::string Protocol_BuildNotification(const std::string& method, const std::string& paramsJson) {
	std::string out;
	out.reserve(64 + method.size() + paramsJson.size());
	out += "{\"jsonrpc\":\"2.0\",\"method\":\"";
	AppendEscaped(out, method);
	out += "\"";
	if (!paramsJson.empty()) {
		out += ",\"params\":";
		out += paramsJson;
	}
	out += "}";
	return out;
}

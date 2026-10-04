#include "BotMethods.h"

#include "Bot.h"

#include "RpcDispatch.h"
#include "RpcRegistry.h"

#include "amxxmodule.h"
#include "parson.h"

#include <string>

// Defined in the vendored SDK (amxxmodule.cpp); not declared in the header.
extern globalvars_t* gpGlobals;

namespace {

JSON_Value* ParseParams(const std::string& paramsJson) {
	if (paramsJson.empty())
		return nullptr;
	JSON_Value* root = json_parse_string(paramsJson.c_str());
	if (!root)
		return nullptr;
	if (json_value_get_type(root) != JSONObject) {
		json_value_free(root);
		return nullptr;
	}
	return root;
}

void ReplyJson(uint32_t handle, JSON_Value* value) {
	if (!value) {
		Rpc_ReplyError(handle, RPC_INTERNAL_ERROR, "internal error");
		return;
	}
	char* serialized = json_serialize_to_string(value);
	std::string out = serialized ? serialized : "null";
	if (serialized)
		json_free_serialized_string(serialized);
	json_value_free(value);
	Rpc_Reply(handle, out);
}

void ReplyOk(uint32_t handle) {
	Rpc_Reply(handle, "{\"ok\":true}");
}

bool GetIndex(JSON_Object* obj, int& index) {
	JSON_Value* value = json_object_get_value(obj, "index");
	if (!value || json_value_get_type(value) != JSONNumber)
		return false;
	index = static_cast<int>(json_value_get_number(value));
	return true;
}

bool ReadNumbers(JSON_Object* obj, const char* key, double out[3]) {
	JSON_Value* value = json_object_get_value(obj, key);
	if (!value || json_value_get_type(value) != JSONArray)
		return false;
	JSON_Array* arr = json_value_get_array(value);
	if (json_array_get_count(arr) != 3)
		return false;
	for (size_t i = 0; i < 3; ++i) {
		JSON_Value* item = json_array_get_value(arr, i);
		if (!item || json_value_get_type(item) != JSONNumber)
			return false;
		out[i] = json_value_get_number(item);
	}
	return true;
}

int MaxClients() {
	return gpGlobals ? gpGlobals->maxClients : 32;
}

// Bounds the index before it reaches YAPB (IsBot on an out-of-range index could
// read past the bot table). Returns false and replies on violation.
bool CheckIndexRange(int index, uint32_t handle) {
	if (index < 1 || index > MaxClients()) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "invalid index");
		return false;
	}
	return true;
}

void HandleAvailable(uint32_t handle) {
	JSON_Value* out = json_value_init_object();
	JSON_Object* obj = json_value_get_object(out);
	bool available = Bot_Available();
	json_object_set_boolean(obj, "available", available);
	if (available) {
		std::string version = Bot_Version();
		if (!version.empty())
			json_object_set_string(obj, "version", version.c_str());
	}
	ReplyJson(handle, out);
}

void HandleAdd(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	JSON_Object* obj = json_value_get_object(root);
	const char* name = json_object_get_string(obj, "name");
	if (!name || !*name) {
		json_value_free(root);
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing name");
		return;
	}
	std::string nameStr = name;

	int difficulty = 1;
	int personality = 0;
	int team = 0;
	JSON_Value* value = json_object_get_value(obj, "difficulty");
	if (value && json_value_get_type(value) == JSONNumber)
		difficulty = static_cast<int>(json_value_get_number(value));
	value = json_object_get_value(obj, "personality");
	if (value && json_value_get_type(value) == JSONNumber)
		personality = static_cast<int>(json_value_get_number(value));
	value = json_object_get_value(obj, "team");
	if (value && json_value_get_type(value) == JSONNumber)
		team = static_cast<int>(json_value_get_number(value));
	json_value_free(root);

	BotResult result = Bot_Add(nameStr, difficulty, personality, team);
	if (!result.ok) {
		Rpc_ReplyError(handle, result.errorCode, result.error);
		return;
	}
	JSON_Value* out = json_value_init_object();
	json_object_set_boolean(json_value_get_object(out), "queued", result.queued);
	ReplyJson(handle, out);
}

void HandleList(uint32_t handle) {
	if (!Bot_Available()) {
		Rpc_ReplyError(handle, RPC_SERVICE_UNAVAILABLE, "YAPB not available");
		return;
	}
	JSON_Value* arrValue = json_value_init_array();
	JSON_Array* arr = json_value_get_array(arrValue);
	int maxClients = MaxClients();
	for (int i = 1; i <= maxClients; ++i) {
		if (!Bot_IsBot(i))
			continue;
		JSON_Value* item = json_value_init_object();
		json_object_set_number(json_value_get_object(item), "index", static_cast<double>(i));
		json_array_append_value(arr, item);
	}
	ReplyJson(handle, arrValue);
}

void HandleGoal(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	JSON_Object* obj = json_value_get_object(root);
	int index = 0;
	if (!GetIndex(obj, index)) {
		json_value_free(root);
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing index");
		return;
	}
	double origin[3];
	bool hasOrigin = ReadNumbers(obj, "origin", origin);
	JSON_Value* nodeValue = json_object_get_value(obj, "node");
	bool hasNode = nodeValue && json_value_get_type(nodeValue) == JSONNumber;
	int node = hasNode ? static_cast<int>(json_value_get_number(nodeValue)) : 0;
	json_value_free(root);

	if (!CheckIndexRange(index, handle))
		return;

	float originF[3] = {0.0f, 0.0f, 0.0f};
	const float* originPtr = nullptr;
	bool useNode = false;
	if (hasOrigin) {
		originF[0] = static_cast<float>(origin[0]);
		originF[1] = static_cast<float>(origin[1]);
		originF[2] = static_cast<float>(origin[2]);
		originPtr = originF;
	} else if (hasNode) {
		useNode = true;
	}

	BotResult result = Bot_Goal(index, originPtr, node, useNode);
	if (!result.ok) {
		Rpc_ReplyError(handle, result.errorCode, result.error);
		return;
	}
	ReplyOk(handle);
}

void HandleLook(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	JSON_Object* obj = json_value_get_object(root);
	int index = 0;
	if (!GetIndex(obj, index)) {
		json_value_free(root);
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing index");
		return;
	}
	double origin[3];
	if (!ReadNumbers(obj, "origin", origin)) {
		json_value_free(root);
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing origin");
		return;
	}
	json_value_free(root);

	if (!CheckIndexRange(index, handle))
		return;

	float originF[3] = {static_cast<float>(origin[0]), static_cast<float>(origin[1]),
	                    static_cast<float>(origin[2])};
	BotResult result = Bot_Look(index, originF);
	if (!result.ok) {
		Rpc_ReplyError(handle, result.errorCode, result.error);
		return;
	}
	ReplyOk(handle);
}

void HandleFreeze(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	JSON_Object* obj = json_value_get_object(root);
	int index = 0;
	if (!GetIndex(obj, index)) {
		json_value_free(root);
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing index");
		return;
	}
	JSON_Value* frozenValue = json_object_get_value(obj, "frozen");
	if (!frozenValue || json_value_get_type(frozenValue) != JSONBoolean) {
		json_value_free(root);
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing frozen");
		return;
	}
	bool frozen = json_value_get_boolean(frozenValue) != 0;
	json_value_free(root);

	if (!CheckIndexRange(index, handle))
		return;

	BotResult result = Bot_Freeze(index, frozen);
	if (!result.ok) {
		Rpc_ReplyError(handle, result.errorCode, result.error);
		return;
	}
	ReplyOk(handle);
}

void HandleStatus(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	int index = 0;
	if (!GetIndex(json_value_get_object(root), index)) {
		json_value_free(root);
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing index");
		return;
	}
	json_value_free(root);

	if (!CheckIndexRange(index, handle))
		return;

	BotStatus status;
	BotResult result = Bot_Status(index, status);
	if (!result.ok) {
		Rpc_ReplyError(handle, result.errorCode, result.error);
		return;
	}

	JSON_Value* out = json_value_init_object();
	JSON_Object* obj = json_value_get_object(out);
	json_object_set_number(obj, "index", static_cast<double>(status.index));
	JSON_Value* originValue = json_value_init_array();
	JSON_Array* origin = json_value_get_array(originValue);
	json_array_append_number(origin, static_cast<double>(status.origin[0]));
	json_array_append_number(origin, static_cast<double>(status.origin[1]));
	json_array_append_number(origin, static_cast<double>(status.origin[2]));
	json_object_set_value(obj, "origin", originValue);
	json_object_set_boolean(obj, "has_origin", status.hasOrigin);
	json_object_set_number(obj, "enemy", static_cast<double>(status.enemy));
	json_object_set_number(obj, "weapon", static_cast<double>(status.weapon));
	json_object_set_number(obj, "task", static_cast<double>(status.task));
	ReplyJson(handle, out);
}

} // namespace

void BotMethods_Init() {
	RpcRegistry* registry = Rpc_GetRegistry();
	if (!registry)
		return;
	registry->AddBuiltin("bot.available", "YAPB availability");
	registry->AddBuiltin("bot.add", "queue a YAPB bot");
	registry->AddBuiltin("bot.list", "list YAPB bots");
	registry->AddBuiltin("bot.goal", "set bot goal origin/node");
	registry->AddBuiltin("bot.look", "set bot look target");
	registry->AddBuiltin("bot.freeze", "freeze/unfreeze bot movement");
	registry->AddBuiltin("bot.status", "read bot state");
}

bool Bot_Dispatch(const std::string& method, const std::string& paramsJson, uint32_t handle) {
	if (method == "bot.available") {
		HandleAvailable(handle);
	} else if (method == "bot.add") {
		HandleAdd(paramsJson, handle);
	} else if (method == "bot.list") {
		HandleList(handle);
	} else if (method == "bot.goal") {
		HandleGoal(paramsJson, handle);
	} else if (method == "bot.look") {
		HandleLook(paramsJson, handle);
	} else if (method == "bot.freeze") {
		HandleFreeze(paramsJson, handle);
	} else if (method == "bot.status") {
		HandleStatus(paramsJson, handle);
	} else {
		return false;
	}
	return true;
}

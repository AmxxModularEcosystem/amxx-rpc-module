#include "FakeMethods.h"

#include "Fake.h"

#include "RpcDispatch.h"
#include "RpcRegistry.h"

#include "in_buttons.h"
#include "parson.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

const double kPi = 3.14159265358979323846;

struct ButtonName {
	const char* name;
	unsigned short bit;
};

const ButtonName kButtons[] = {
	{"IN_ATTACK", IN_ATTACK},
	{"IN_JUMP", IN_JUMP},
	{"IN_DUCK", IN_DUCK},
	{"IN_FORWARD", IN_FORWARD},
	{"IN_BACK", IN_BACK},
	{"IN_USE", IN_USE},
	{"IN_CANCEL", IN_CANCEL},
	{"IN_LEFT", IN_LEFT},
	{"IN_RIGHT", IN_RIGHT},
	{"IN_MOVELEFT", IN_MOVELEFT},
	{"IN_MOVERIGHT", IN_MOVERIGHT},
	{"IN_ATTACK2", IN_ATTACK2},
	{"IN_RUN", IN_RUN},
	{"IN_RELOAD", IN_RELOAD},
	{"IN_ALT1", IN_ALT1},
	{"IN_SCORE", IN_SCORE},
};

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

// Resolves a fake by index; replies with an error and returns nullptr when the
// index is not a registered fake (real players are rejected, FR-FAKE-009).
FakeRecord* ResolveFake(JSON_Object* obj, uint32_t handle) {
	int index = 0;
	if (!GetIndex(obj, index)) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing index");
		return nullptr;
	}
	FakeRecord* rec = Fake_GetRecord(index);
	if (!rec) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "index is not a registered fake");
		return nullptr;
	}
	return rec;
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

bool ButtonBit(JSON_Value* value, unsigned short& bit) {
	if (!value)
		return false;
	if (json_value_get_type(value) == JSONNumber) {
		bit = static_cast<unsigned short>(json_value_get_number(value));
		return true;
	}
	if (json_value_get_type(value) == JSONString) {
		const char* name = json_value_get_string(value);
		if (!name)
			return false;
		for (size_t i = 0; i < sizeof(kButtons) / sizeof(kButtons[0]); ++i) {
			if (std::string(name) == kButtons[i].name) {
				bit = kButtons[i].bit;
				return true;
			}
		}
	}
	return false;
}

JSON_Value* BuildFakeValue(const FakeRecord& rec) {
	JSON_Value* objValue = json_value_init_object();
	if (!objValue)
		return nullptr;
	JSON_Object* obj = json_value_get_object(objValue);
	json_object_set_number(obj, "index", static_cast<double>(rec.entIndex));
	json_object_set_string(obj, "name", rec.name.c_str());
	json_object_set_string(obj, "authid", rec.authid.c_str());
	json_object_set_boolean(obj, "alive", rec.alive);

	entvars_t* pev = rec.ent ? &rec.ent->v : nullptr;
	if (pev) {
		json_object_set_number(obj, "health", static_cast<double>(pev->health));
		json_object_set_number(obj, "armor", static_cast<double>(pev->armorvalue));
		json_object_set_number(obj, "team", static_cast<double>(pev->team));
		JSON_Value* originValue = json_value_init_array();
		JSON_Array* origin = json_value_get_array(originValue);
		json_array_append_number(origin, static_cast<double>(pev->origin[0]));
		json_array_append_number(origin, static_cast<double>(pev->origin[1]));
		json_array_append_number(origin, static_cast<double>(pev->origin[2]));
		json_object_set_value(obj, "origin", originValue);
	}

	JSON_Value* anglesValue = json_value_init_array();
	JSON_Array* angles = json_value_get_array(anglesValue);
	json_array_append_number(angles, static_cast<double>(rec.viewAngles[0]));
	json_array_append_number(angles, static_cast<double>(rec.viewAngles[1]));
	json_array_append_number(angles, static_cast<double>(rec.viewAngles[2]));
	json_object_set_value(obj, "angles", anglesValue);
	return objValue;
}

void HandleCreate(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	JSON_Object* obj = json_value_get_object(root);
	const char* name = json_object_get_string(obj, "name");
	const char* authid = json_object_get_string(obj, "authid");
	if (!name || !*name) {
		json_value_free(root);
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing name");
		return;
	}

	FakeCreateResult created = Fake_Create(name, authid ? authid : "");
	if (!created.ok) {
		json_value_free(root);
		Rpc_ReplyError(handle, created.errorCode, created.error);
		return;
	}

	JSON_Value* teamValue = json_object_get_value(obj, "team");
	if (teamValue && json_value_get_type(teamValue) == JSONNumber) {
		FakeRecord* rec = Fake_GetRecord(created.index);
		if (rec && rec->ent)
			rec->ent->v.team = static_cast<int>(json_value_get_number(teamValue));
	}
	json_value_free(root);

	JSON_Value* out = json_value_init_object();
	JSON_Object* o = json_value_get_object(out);
	json_object_set_number(o, "index", static_cast<double>(created.index));
	json_object_set_string(o, "authid", created.authid.c_str());
	ReplyJson(handle, out);
}

void HandleRemove(const std::string& paramsJson, uint32_t handle) {
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

	int errorCode = 0;
	std::string error;
	if (!Fake_Remove(index, errorCode, error)) {
		Rpc_ReplyError(handle, errorCode, error);
		return;
	}
	ReplyOk(handle);
}

void HandleList(uint32_t handle) {
	JSON_Value* arrValue = json_value_init_array();
	JSON_Array* arr = json_value_get_array(arrValue);
	std::vector<int> indices = Fake_Indices();
	for (size_t i = 0; i < indices.size(); ++i) {
		FakeRecord* rec = Fake_GetRecord(indices[i]);
		if (!rec)
			continue;
		JSON_Value* value = BuildFakeValue(*rec);
		if (value)
			json_array_append_value(arr, value);
	}
	ReplyJson(handle, arrValue);
}

void HandleGet(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	FakeRecord* rec = ResolveFake(json_value_get_object(root), handle);
	if (!rec) {
		json_value_free(root);
		return;
	}
	JSON_Value* out = BuildFakeValue(*rec);
	json_value_free(root);
	ReplyJson(handle, out);
}

void HandleMove(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	JSON_Object* obj = json_value_get_object(root);
	FakeRecord* rec = ResolveFake(obj, handle);
	if (!rec) {
		json_value_free(root);
		return;
	}
	JSON_Value* fwd = json_object_get_value(obj, "forward");
	JSON_Value* side = json_object_get_value(obj, "side");
	JSON_Value* up = json_object_get_value(obj, "up");
	if (fwd && json_value_get_type(fwd) == JSONNumber)
		rec->fwd = static_cast<float>(json_value_get_number(fwd));
	if (side && json_value_get_type(side) == JSONNumber)
		rec->side = static_cast<float>(json_value_get_number(side));
	if (up && json_value_get_type(up) == JSONNumber)
		rec->up = static_cast<float>(json_value_get_number(up));
	json_value_free(root);
	ReplyOk(handle);
}

void HandleLook(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	JSON_Object* obj = json_value_get_object(root);
	FakeRecord* rec = ResolveFake(obj, handle);
	if (!rec) {
		json_value_free(root);
		return;
	}

	double angles[3];
	double at[3];
	if (ReadNumbers(obj, "angles", angles)) {
		rec->viewAngles[0] = static_cast<float>(angles[0]);
		rec->viewAngles[1] = static_cast<float>(angles[1]);
		rec->viewAngles[2] = static_cast<float>(angles[2]);
	} else if (ReadNumbers(obj, "at", at) && rec->ent) {
		entvars_t* pev = &rec->ent->v;
		double dx = at[0] - pev->origin[0];
		double dy = at[1] - pev->origin[1];
		double dz = at[2] - pev->origin[2];
		double horiz = std::sqrt(dx * dx + dy * dy);
		rec->viewAngles[0] = static_cast<float>(-std::atan2(dz, horiz) * 180.0 / kPi);
		rec->viewAngles[1] = static_cast<float>(std::atan2(dy, dx) * 180.0 / kPi);
		rec->viewAngles[2] = 0.0f;
	} else {
		json_value_free(root);
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "missing angles or at");
		return;
	}
	json_value_free(root);
	ReplyOk(handle);
}

void HandleStop(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	FakeRecord* rec = ResolveFake(json_value_get_object(root), handle);
	if (!rec) {
		json_value_free(root);
		return;
	}
	rec->fwd = 0.0f;
	rec->side = 0.0f;
	rec->up = 0.0f;
	rec->buttons = 0;
	rec->impulse = 0;
	json_value_free(root);
	ReplyOk(handle);
}

void HandleButtons(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	JSON_Object* obj = json_value_get_object(root);
	FakeRecord* rec = ResolveFake(obj, handle);
	if (!rec) {
		json_value_free(root);
		return;
	}

	JSON_Value* press = json_object_get_value(obj, "press");
	if (press && json_value_get_type(press) == JSONArray) {
		JSON_Array* arr = json_value_get_array(press);
		for (size_t i = 0; i < json_array_get_count(arr); ++i) {
			unsigned short bit = 0;
			if (ButtonBit(json_array_get_value(arr, i), bit))
				rec->buttons |= bit;
		}
	}
	JSON_Value* release = json_object_get_value(obj, "release");
	if (release && json_value_get_type(release) == JSONArray) {
		JSON_Array* arr = json_value_get_array(release);
		for (size_t i = 0; i < json_array_get_count(arr); ++i) {
			unsigned short bit = 0;
			if (ButtonBit(json_array_get_value(arr, i), bit))
				rec->buttons &= static_cast<unsigned short>(~bit);
		}
	}
	json_value_free(root);
	ReplyOk(handle);
}

void HandleSet(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	JSON_Object* obj = json_value_get_object(root);
	FakeRecord* rec = ResolveFake(obj, handle);
	if (!rec || !rec->ent) {
		json_value_free(root);
		return;
	}
	entvars_t* pev = &rec->ent->v;

	JSON_Value* health = json_object_get_value(obj, "health");
	if (health && json_value_get_type(health) == JSONNumber)
		pev->health = static_cast<float>(json_value_get_number(health));
	JSON_Value* armor = json_object_get_value(obj, "armor");
	if (armor && json_value_get_type(armor) == JSONNumber)
		pev->armorvalue = static_cast<float>(json_value_get_number(armor));
	JSON_Value* team = json_object_get_value(obj, "team");
	if (team && json_value_get_type(team) == JSONNumber)
		pev->team = static_cast<int>(json_value_get_number(team));
	JSON_Value* weapon = json_object_get_value(obj, "weapon");
	if (weapon && json_value_get_type(weapon) == JSONString) {
		const char* name = json_value_get_string(weapon);
		if (name && *name)
			g_engfuncs.pfnClientCommand(rec->ent, "give %s\n", name);
	}
	json_value_free(root);
	ReplyOk(handle);
}

void HandleAuthid(const std::string& paramsJson, uint32_t handle) {
	JSON_Value* root = ParseParams(paramsJson);
	if (!root) {
		Rpc_ReplyError(handle, RPC_INVALID_PARAMS, "params must be an object");
		return;
	}
	JSON_Object* obj = json_value_get_object(root);
	FakeRecord* rec = ResolveFake(obj, handle);
	if (!rec) {
		json_value_free(root);
		return;
	}
	const char* authid = json_object_get_string(obj, "authid");
	if (authid && *authid)
		rec->authid = authid;

	JSON_Value* out = json_value_init_object();
	JSON_Object* o = json_value_get_object(out);
	json_object_set_number(o, "index", static_cast<double>(rec->entIndex));
	json_object_set_string(o, "authid", rec->authid.c_str());
	json_value_free(root);
	ReplyJson(handle, out);
}

} // namespace

void FakeMethods_Init() {
	RpcRegistry* registry = Rpc_GetRegistry();
	if (!registry)
		return;
	registry->AddBuiltin("fake.create", "create a fake player");
	registry->AddBuiltin("fake.remove", "remove a fake player");
	registry->AddBuiltin("fake.list", "list fake players");
	registry->AddBuiltin("fake.get", "get one fake player");
	registry->AddBuiltin("fake.move", "set fake movement");
	registry->AddBuiltin("fake.look", "set fake view angles");
	registry->AddBuiltin("fake.stop", "stop fake movement");
	registry->AddBuiltin("fake.buttons", "press/release fake buttons");
	registry->AddBuiltin("fake.set", "set fake state");
	registry->AddBuiltin("fake.authid", "get/set fake authid");
}

bool Fake_Dispatch(const std::string& method, const std::string& paramsJson, uint32_t handle) {
	if (method == "fake.create") {
		HandleCreate(paramsJson, handle);
	} else if (method == "fake.remove") {
		HandleRemove(paramsJson, handle);
	} else if (method == "fake.list") {
		HandleList(handle);
	} else if (method == "fake.get") {
		HandleGet(paramsJson, handle);
	} else if (method == "fake.move") {
		HandleMove(paramsJson, handle);
	} else if (method == "fake.look") {
		HandleLook(paramsJson, handle);
	} else if (method == "fake.stop") {
		HandleStop(paramsJson, handle);
	} else if (method == "fake.buttons") {
		HandleButtons(paramsJson, handle);
	} else if (method == "fake.set") {
		HandleSet(paramsJson, handle);
	} else if (method == "fake.authid") {
		HandleAuthid(paramsJson, handle);
	} else {
		return false;
	}
	return true;
}

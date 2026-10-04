#include "RpcRegistry.h"

#include "parson.h"

namespace {

const char* SourceName(RpcMethodSource source) {
	switch (source) {
		case RPC_SOURCE_PAWN:      return "pawn";
		case RPC_SOURCE_TRANSPORT: return "transport";
		case RPC_SOURCE_BUILTIN:
		default:                   return "builtin";
	}
}

} // namespace

bool RpcRegistry::AddBuiltin(const std::string& name, const std::string& description) {
	if (name.empty() || m_methods.count(name))
		return false;
	RpcMethodInfo info;
	info.name = name;
	info.source = RPC_SOURCE_BUILTIN;
	info.description = description;
	m_methods[name] = info;
	return true;
}

bool RpcRegistry::AddTransport(const std::string& name, const std::string& description) {
	if (name.empty() || m_methods.count(name))
		return false;
	RpcMethodInfo info;
	info.name = name;
	info.source = RPC_SOURCE_TRANSPORT;
	info.description = description;
	m_methods[name] = info;
	return true;
}

bool RpcRegistry::AddPawn(const std::string& name, const std::string& callback,
                          const std::string& description, int pluginId) {
	if (name.empty() || callback.empty())
		return false;
	if (IsCoreName(name))
		return false;
	if (m_methods.count(name))
		return false; // duplicate Pawn name
	if (!m_registrar)
		return false;
	int forwardId = m_registrar->RegisterForward(pluginId, callback);
	if (forwardId < 0)
		return false;
	RpcMethodInfo info;
	info.name = name;
	info.source = RPC_SOURCE_PAWN;
	info.description = description;
	info.pluginId = pluginId;
	info.forwardId = forwardId;
	m_methods[name] = info;
	return true;
}

bool RpcRegistry::RemovePawn(const std::string& name, int pluginId) {
	std::map<std::string, RpcMethodInfo>::iterator it = m_methods.find(name);
	if (it == m_methods.end())
		return false;
	if (it->second.source != RPC_SOURCE_PAWN)
		return false;
	if (it->second.pluginId != pluginId)
		return false;
	if (m_registrar && it->second.forwardId >= 0)
		m_registrar->UnregisterForward(it->second.forwardId);
	m_methods.erase(it);
	return true;
}

int RpcRegistry::RemoveAllPawn() {
	int removed = 0;
	for (std::map<std::string, RpcMethodInfo>::iterator it = m_methods.begin();
	     it != m_methods.end();) {
		if (it->second.source == RPC_SOURCE_PAWN) {
			if (m_registrar && it->second.forwardId >= 0)
				m_registrar->UnregisterForward(it->second.forwardId);
			it = m_methods.erase(it);
			++removed;
		} else {
			++it;
		}
	}
	return removed;
}

const RpcMethodInfo* RpcRegistry::Find(const std::string& name) const {
	std::map<std::string, RpcMethodInfo>::const_iterator it = m_methods.find(name);
	if (it == m_methods.end())
		return nullptr;
	return &it->second;
}

bool RpcRegistry::IsCoreName(const std::string& name) const {
	if (name.compare(0, 4, "rpc.") == 0)
		return true;
	std::map<std::string, RpcMethodInfo>::const_iterator it = m_methods.find(name);
	if (it != m_methods.end() && it->second.source != RPC_SOURCE_PAWN)
		return true;
	return false;
}

std::string RpcRegistry::CatalogJson() const {
	JSON_Value* root = json_value_init_array();
	if (!root)
		return "[]";
	JSON_Array* arr = json_value_get_array(root);
	for (std::map<std::string, RpcMethodInfo>::const_iterator it = m_methods.begin();
	     it != m_methods.end(); ++it) {
		JSON_Value* objValue = json_value_init_object();
		if (!objValue)
			continue;
		JSON_Object* obj = json_value_get_object(objValue);
		json_object_set_string(obj, "name", it->second.name.c_str());
		json_object_set_string(obj, "source", SourceName(it->second.source));
		json_object_set_string(obj, "description", it->second.description.c_str());
		json_array_append_value(arr, objValue);
	}
	char* serialized = json_serialize_to_string(root);
	std::string out = serialized ? serialized : "[]";
	if (serialized)
		json_free_serialized_string(serialized);
	json_value_free(root);
	return out;
}

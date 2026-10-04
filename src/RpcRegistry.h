#ifndef AMXXRPC_RPCREGISTRY_H
#define AMXXRPC_RPCREGISTRY_H

// RPC method registry (design/12 §1). Deliberately decoupled from AMXX: Pawn
// method registration goes through an injectable IRpcForwardRegistrar so the
// registry can be unit-tested off-line (tests/registry_test.cpp).

#include <map>
#include <string>

enum RpcMethodSource {
	RPC_SOURCE_BUILTIN = 0,
	RPC_SOURCE_PAWN,
	RPC_SOURCE_TRANSPORT,
};

struct RpcMethodInfo {
	std::string name;
	RpcMethodSource source = RPC_SOURCE_BUILTIN;
	std::string description;
	int pluginId = -1;   // Pawn owner; -1 for builtin/transport
	int forwardId = -1;  // SP forward id; -1 for builtin/transport
};

// Forward registrar abstraction. The AMXX implementation lives in PawnApi.cpp;
// tests supply a fake. RegisterForward returns a forward id or -1 on failure.
class IRpcForwardRegistrar {
public:
	virtual ~IRpcForwardRegistrar() {}
	virtual int RegisterForward(int pluginId, const std::string& callback) = 0;
	virtual void UnregisterForward(int forwardId) = 0;
};

class RpcRegistry {
public:
	RpcRegistry() : m_registrar(nullptr) {}
	explicit RpcRegistry(IRpcForwardRegistrar* registrar) : m_registrar(registrar) {}

	void SetRegistrar(IRpcForwardRegistrar* registrar) { m_registrar = registrar; }

	// Builtin/transport methods (no forward). Duplicate names are rejected.
	bool AddBuiltin(const std::string& name, const std::string& description);
	bool AddTransport(const std::string& name, const std::string& description);

	// Pawn method. Fails on empty name/callback, duplicate, core-override, or
	// when the forward cannot be registered (registrar returns -1).
	bool AddPawn(const std::string& name, const std::string& callback,
	             const std::string& description, int pluginId);

	// Removes a Pawn method owned by pluginId. Returns false if not found or
	// not owned by pluginId.
	bool RemovePawn(const std::string& name, int pluginId);

	// Removes every Pawn method (global plugin unload). Returns the count.
	int RemoveAllPawn();

	const RpcMethodInfo* Find(const std::string& name) const;

	// True when `name` is reserved for core (rpc.* namespace or an existing
	// builtin/transport entry) and therefore cannot be overridden by Pawn.
	bool IsCoreName(const std::string& name) const;

	// Catalog for rpc.methods: a bare JSON array [{name,source,description}].
	std::string CatalogJson() const;

	size_t Size() const { return m_methods.size(); }
	void Clear() { m_methods.clear(); }

private:
	std::map<std::string, RpcMethodInfo> m_methods;
	IRpcForwardRegistrar* m_registrar;
};

#endif // AMXXRPC_RPCREGISTRY_H

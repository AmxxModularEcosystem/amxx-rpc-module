#ifndef AMXXRPC_CONFIG_H
#define AMXXRPC_CONFIG_H

// Configuration parsing/validation/reload (design/16 §1–3).
// Pure C++ (no AMXX): the caller supplies the config path.

#include "Log.h"

#include <string>

struct Config {
	std::string host = "127.0.0.1";
	int port = 27016;
	std::string token;
	int maxConnections = 4;
	int idleTimeout = 120;
	int preAuthTimeout = 5;
	long long maxMessageBytes = 1048576;
	long long maxSessionOutBytes = 0; // 0 => computed as maxMessageBytes * outboxDepth
	int requestTimeout = 15;
	long long inboxDepth = 256;
	long long outboxDepth = 256;
	int drainBudget = 64;
	int fakeMax = 8;
	std::string yapbPath;
	bool yapbSelfLoad = false; // off by default; loading YAPB as a plain lib is risky (design/14 §9 B2)
	LogLevel logLevel = ARP_LOG_INFO;
};

// Parses the file and validates it. On failure returns false and fills error.
// Unknown keys are warned about and ignored (forward-compat).
bool Config_Load(const std::string& path, Config& out, std::string& error);

// Validates and fills computed defaults (maxSessionOutBytes). Returns false on
// fail-fast violations (missing/short token, bad port, negative limits, ...).
bool Config_Validate(Config& cfg, std::string& error);

// True when a restart-only key (host/port/token) differs.
bool Config_RestartKeysChanged(const Config& a, const Config& b);

// Copies live-reloadable keys (log_level, idle_timeout, request_timeout,
// drain_budget, max_connections) from `from` into `to`.
void Config_ApplyLive(const Config& from, Config& to);

#endif // AMXXRPC_CONFIG_H

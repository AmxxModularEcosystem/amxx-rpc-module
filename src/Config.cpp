#include "Config.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
	#include <winsock2.h>
#else
	#include <sys/select.h>
#endif

namespace {

std::string Trim(const std::string& s) {
	size_t begin = 0;
	size_t end = s.size();
	while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])))
		++begin;
	while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])))
		--end;
	return s.substr(begin, end - begin);
}

std::string StripQuotes(const std::string& s) {
	if (s.size() >= 2) {
		char first = s.front();
		char last = s.back();
		if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
			return s.substr(1, s.size() - 2);
	}
	return s;
}

bool ParseInt(const std::string& s, long long& out) {
	if (s.empty())
		return false;
	char* end = nullptr;
	errno = 0;
	long long value = std::strtoll(s.c_str(), &end, 10);
	if (errno != 0 || end == s.c_str() || *end != '\0')
		return false;
	out = value;
	return true;
}

bool ParseLogLevel(const std::string& s, LogLevel& out) {
	if (s == "error") { out = ARP_LOG_ERROR; return true; }
	if (s == "warn")  { out = ARP_LOG_WARN;  return true; }
	if (s == "info")  { out = ARP_LOG_INFO;  return true; }
	if (s == "debug") { out = ARP_LOG_DEBUG; return true; }
	return false;
}

bool ParseBool(const std::string& s, bool& out) {
	if (s == "1" || s == "true" || s == "yes" || s == "on")  { out = true;  return true; }
	if (s == "0" || s == "false" || s == "no" || s == "off") { out = false; return true; }
	return false;
}

bool IsLoopbackHost(const std::string& host) {
	return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

} // namespace

bool Config_Load(const std::string& path, Config& out, std::string& error) {
	std::ifstream file(path.c_str());
	if (!file.is_open()) {
		error = "cannot open config file: " + path;
		return false;
	}

	out = Config();
	std::string line;
	int lineNo = 0;
	while (std::getline(file, line)) {
		++lineNo;
		if (lineNo == 1 && line.size() >= 3 &&
		    static_cast<unsigned char>(line[0]) == 0xEF &&
		    static_cast<unsigned char>(line[1]) == 0xBB &&
		    static_cast<unsigned char>(line[2]) == 0xBF) {
			line.erase(0, 3); // UTF-8 BOM
		}
		std::string trimmed = Trim(line);
		if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';')
			continue;

		size_t eq = trimmed.find('=');
		if (eq == std::string::npos) {
			error = "line " + std::to_string(lineNo) + ": expected key=value";
			return false;
		}
		std::string key = Trim(trimmed.substr(0, eq));
		std::string value = StripQuotes(Trim(trimmed.substr(eq + 1)));
		if (key.empty()) {
			error = "line " + std::to_string(lineNo) + ": empty key";
			return false;
		}

		long long number = 0;
		if (key == "host") {
			out.host = value;
		} else if (key == "port") {
			if (!ParseInt(value, number)) { error = "line " + std::to_string(lineNo) + ": invalid port"; return false; }
			out.port = static_cast<int>(number);
		} else if (key == "token") {
			out.token = value;
		} else if (key == "max_connections") {
			if (!ParseInt(value, number)) { error = "line " + std::to_string(lineNo) + ": invalid max_connections"; return false; }
			out.maxConnections = static_cast<int>(number);
		} else if (key == "idle_timeout") {
			if (!ParseInt(value, number)) { error = "line " + std::to_string(lineNo) + ": invalid idle_timeout"; return false; }
			out.idleTimeout = static_cast<int>(number);
		} else if (key == "pre_auth_timeout") {
			if (!ParseInt(value, number)) { error = "line " + std::to_string(lineNo) + ": invalid pre_auth_timeout"; return false; }
			out.preAuthTimeout = static_cast<int>(number);
		} else if (key == "max_message_bytes") {
			if (!ParseInt(value, number)) { error = "line " + std::to_string(lineNo) + ": invalid max_message_bytes"; return false; }
			out.maxMessageBytes = number;
		} else if (key == "max_session_out_bytes") {
			if (!ParseInt(value, number)) { error = "line " + std::to_string(lineNo) + ": invalid max_session_out_bytes"; return false; }
			out.maxSessionOutBytes = number;
		} else if (key == "request_timeout") {
			if (!ParseInt(value, number)) { error = "line " + std::to_string(lineNo) + ": invalid request_timeout"; return false; }
			out.requestTimeout = static_cast<int>(number);
		} else if (key == "inbox_depth") {
			if (!ParseInt(value, number)) { error = "line " + std::to_string(lineNo) + ": invalid inbox_depth"; return false; }
			out.inboxDepth = number;
		} else if (key == "outbox_depth") {
			if (!ParseInt(value, number)) { error = "line " + std::to_string(lineNo) + ": invalid outbox_depth"; return false; }
			out.outboxDepth = number;
		} else if (key == "drain_budget") {
			if (!ParseInt(value, number)) { error = "line " + std::to_string(lineNo) + ": invalid drain_budget"; return false; }
			out.drainBudget = static_cast<int>(number);
		} else if (key == "fake_max") {
			if (!ParseInt(value, number)) { error = "line " + std::to_string(lineNo) + ": invalid fake_max"; return false; }
			out.fakeMax = static_cast<int>(number);
		} else if (key == "yapb_path") {
			out.yapbPath = value;
		} else if (key == "yapb_self_load") {
			if (!ParseBool(value, out.yapbSelfLoad)) {
				error = "line " + std::to_string(lineNo) + ": invalid yapb_self_load (true/false)";
				return false;
			}
		} else if (key == "log_level") {
			if (!ParseLogLevel(value, out.logLevel)) {
				error = "line " + std::to_string(lineNo) + ": invalid log_level (error/warn/info/debug)";
				return false;
			}
		} else {
			Log_Write(ARP_LOG_WARN, "config: unknown key '%s' ignored (line %d)", key.c_str(), lineNo);
		}
	}

	return Config_Validate(out, error);
}

bool Config_Validate(Config& cfg, std::string& error) {
	if (cfg.token.size() < 16) {
		error = "token must be at least 16 characters";
		return false;
	}
	if (cfg.port < 1 || cfg.port > 65535) {
		error = "port must be in 1..65535";
		return false;
	}
	if (cfg.maxConnections < 1) {
		error = "max_connections must be >= 1";
		return false;
	}
	if (cfg.maxConnections > FD_SETSIZE - 1) {
		error = "max_connections must be <= FD_SETSIZE-1";
		return false;
	}
	if (cfg.idleTimeout < 1) {
		error = "idle_timeout must be >= 1";
		return false;
	}
	if (cfg.preAuthTimeout < 1) {
		error = "pre_auth_timeout must be >= 1";
		return false;
	}
	if (cfg.preAuthTimeout >= cfg.idleTimeout) {
		Log_Write(ARP_LOG_WARN, "config: pre_auth_timeout (%d) >= idle_timeout (%d); clamping",
		          cfg.preAuthTimeout, cfg.idleTimeout);
		cfg.preAuthTimeout = cfg.idleTimeout > 1 ? cfg.idleTimeout - 1 : 1;
	}
	if (cfg.maxMessageBytes < 1) {
		error = "max_message_bytes must be >= 1";
		return false;
	}
	if (cfg.requestTimeout < 1) {
		error = "request_timeout must be >= 1";
		return false;
	}
	if (cfg.inboxDepth < 1) {
		error = "inbox_depth must be >= 1";
		return false;
	}
	if (cfg.outboxDepth < 1) {
		error = "outbox_depth must be >= 1";
		return false;
	}
	if (cfg.drainBudget < 1) {
		error = "drain_budget must be >= 1";
		return false;
	}
	if (cfg.fakeMax < 0) {
		error = "fake_max must be >= 0";
		return false;
	}
	if (cfg.maxSessionOutBytes <= 0)
		cfg.maxSessionOutBytes = cfg.maxMessageBytes * cfg.outboxDepth;
	if (cfg.maxSessionOutBytes < cfg.maxMessageBytes) {
		Log_Write(ARP_LOG_WARN, "config: max_session_out_bytes (%lld) < max_message_bytes (%lld)",
		          cfg.maxSessionOutBytes, cfg.maxMessageBytes);
	}
	if (!IsLoopbackHost(cfg.host)) {
		Log_Write(ARP_LOG_WARN, "config: host '%s' is not loopback; token is sent in cleartext (R-4)",
		          cfg.host.c_str());
	}
	return true;
}

bool Config_RestartKeysChanged(const Config& a, const Config& b) {
	return a.host != b.host || a.port != b.port || a.token != b.token;
}

void Config_ApplyLive(const Config& from, Config& to) {
	to.logLevel = from.logLevel;
	to.idleTimeout = from.idleTimeout;
	to.requestTimeout = from.requestTimeout;
	to.drainBudget = from.drainBudget;
	to.maxConnections = from.maxConnections;
}

#include "Log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>

namespace {

std::mutex g_mutex;
std::FILE* g_file = nullptr;
std::atomic<int> g_level(ARP_LOG_INFO);
std::deque<std::string> g_queue;
const size_t kQueueCap = 256;
size_t g_dropped = 0;

const char* LevelName(LogLevel level) {
	switch (level) {
		case ARP_LOG_ERROR: return "ERROR";
		case ARP_LOG_WARN:  return "WARN";
		case ARP_LOG_INFO:  return "INFO";
		case ARP_LOG_DEBUG: return "DEBUG";
		default:        return "?";
	}
}

void FormatTimestamp(char* out, size_t outSize) {
	std::time_t now = std::time(nullptr);
	std::tm tmv;
#if defined(_WIN32)
	localtime_s(&tmv, &now);
#else
	localtime_r(&now, &tmv);
#endif
	std::strftime(out, outSize, "%Y-%m-%d %H:%M:%S", &tmv);
}

} // namespace

void Log_Init(const std::string& filePath, LogLevel level) {
	std::lock_guard<std::mutex> lock(g_mutex);
	g_level.store(level);
	if (g_file) {
		std::fclose(g_file);
		g_file = nullptr;
	}
	if (!filePath.empty())
		g_file = std::fopen(filePath.c_str(), "a");
}

void Log_Shutdown() {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_file) {
		std::fflush(g_file);
		std::fclose(g_file);
		g_file = nullptr;
	}
	g_queue.clear();
	g_dropped = 0;
}

void Log_SetLevel(LogLevel level) {
	g_level.store(level);
}

LogLevel Log_GetLevel() {
	return static_cast<LogLevel>(g_level.load());
}

void Log_Write(LogLevel level, const char* fmt, ...) {
	if (level > g_level.load())
		return;

	char message[2048];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	message[sizeof(message) - 1] = '\0';

	char timestamp[32];
	FormatTimestamp(timestamp, sizeof(timestamp));

	std::string line;
	line.reserve(64 + std::strlen(message));
	line += '[';
	line += timestamp;
	line += "] [";
	line += LevelName(level);
	line += "] ";
	line += message;

	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_file) {
		std::fprintf(g_file, "%s\n", line.c_str());
		std::fflush(g_file);
	}
	if (g_queue.size() >= kQueueCap) {
		++g_dropped;
	} else {
		if (g_dropped > 0) {
			g_queue.push_back("[AmxxRpc] [WARN] log queue overflow: " +
			                  std::to_string(g_dropped) + " line(s) dropped");
			g_dropped = 0;
		}
		g_queue.push_back(line);
	}
}

bool Log_PopLine(std::string& out) {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_queue.empty())
		return false;
	out = std::move(g_queue.front());
	g_queue.pop_front();
	return true;
}

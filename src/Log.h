#ifndef AMXXRPC_LOG_H
#define AMXXRPC_LOG_H

// Thread-safe logging (design/11 §7, design/16 §5).
// The I/O thread writes to a file and to a bounded queue; the main thread drains
// the queue via Log_PopLine() and forwards to MF_Log. Log_* never calls AMXX.
// Secrets (the token) are never logged.

#include <string>

enum LogLevel {
	ARP_LOG_ERROR = 0,
	ARP_LOG_WARN  = 1,
	ARP_LOG_INFO  = 2,
	ARP_LOG_DEBUG = 3,
};

// Opens the log file (append). Safe to call once from the main thread.
void Log_Init(const std::string& filePath, LogLevel level);
void Log_Shutdown();

void Log_SetLevel(LogLevel level);
LogLevel Log_GetLevel();

// Thread-safe. Formats "[ts] [LEVEL] message" and writes to file + queue.
void Log_Write(LogLevel level, const char* fmt, ...);

// Pops one queued line for the main thread to forward to MF_Log.
bool Log_PopLine(std::string& out);

#endif // AMXXRPC_LOG_H

/*
 * Copyright (c) 2026 Cronfox
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QUICKSHIORI_LOG_H
#define QUICKSHIORI_LOG_H

#include <string>

// ---------------------------------------------------------------------------
// Log levels (ascending severity)
// ---------------------------------------------------------------------------
// NONE disables all output.
enum class LogLevel {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERR   = 3,
    NONE  = 4
};

// ---------------------------------------------------------------------------
// Shared state (defined in quickshiori.cpp, visible within the DLL)
// ---------------------------------------------------------------------------
// Minimum level to record. Messages below this level are dropped.
extern LogLevel g_log_level;
// When false, nothing is written to the log file on disk.
extern bool     g_log_to_file;

// ---------------------------------------------------------------------------
// Logging interface
// ---------------------------------------------------------------------------
const char* log_level_str(LogLevel lv);

// Core function — checks level, writes debugger output and (optionally) file.
void log_write(LogLevel lv, const std::string& msg);

// Convenience wrappers.
inline void log_debug(const std::string& m) { log_write(LogLevel::DEBUG, m); }
inline void log_info (const std::string& m) { log_write(LogLevel::INFO,  m); }
inline void log_warn (const std::string& m) { log_write(LogLevel::WARN,  m); }
inline void log_error(const std::string& m) { log_write(LogLevel::ERR,   m); }

// Parse a level string ("debug"|"info"|"warn"|"error") case-insensitively.
// Returns LogLevel::INFO for any unrecognised value.
LogLevel log_level_from_str(const char* s);

#endif // QUICKSHIORI_LOG_H

/*
 * Copyright (c) 2026 Cronfox
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

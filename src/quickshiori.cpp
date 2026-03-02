/*
 * Copyright (c) 2026 Cronfox
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
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

#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cctype>
#include "quickjs.h"
#include "quickjs-libc.h"
#include "quickshiori_log.h"
#include "quickshiori_module.h"
// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static JSRuntime* g_rt  = nullptr;
static JSContext* g_ctx = nullptr;
static std::string g_dir;        // module directory, UTF-8, no trailing slash
static bool g_initialized = false;

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

// CP_OEMCP (OEM codepage) -> UTF-8
static std::string cp_oemcp_to_utf8(const char* src, int len) {
    int wlen = MultiByteToWideChar(CP_OEMCP, 0, src, len, nullptr, 0);
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_OEMCP, 0, src, len, wbuf.data(), wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, nullptr, 0, nullptr, nullptr);
    std::vector<char> ubuf(ulen);
    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, ubuf.data(), ulen, nullptr, nullptr);
    return {ubuf.begin(), ubuf.end()};
}

// Build a HGLOBAL from a std::string. Caller (base ware) owns the memory.
static HGLOBAL to_hglobal(const std::string& s, long* len) {
    *len = static_cast<long>(s.size());
    HGLOBAL h = GlobalAlloc(GMEM_FIXED, *len + 1); // +1 for optional null byte
    if (h) {
        memcpy(reinterpret_cast<char*>(h), s.data(), *len);
        reinterpret_cast<char*>(h)[*len] = '\0';
    }
    return h;
}

// Read a whole file into a string.
static bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// ---------------------------------------------------------------------------
// Logging  (interface declared in quickshiori_log.h)
// ---------------------------------------------------------------------------

// Definitions of the two globals declared extern in quickshiori_log.h.
LogLevel g_log_level   = LogLevel::INFO;
bool     g_log_to_file = true;

const char* log_level_str(LogLevel lv) {
    switch (lv) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERR:   return "ERROR";
        default:              return "?????";
    }
}

LogLevel log_level_from_str(const char* s) {
    if (!s) return LogLevel::INFO;
    std::string ls;
    for (const char* p = s; *p; ++p)
        ls += (char)tolower((unsigned char)*p);
    if (ls == "debug") return LogLevel::DEBUG;
    if (ls == "warn")  return LogLevel::WARN;
    if (ls == "error") return LogLevel::ERR;
    if (ls == "none")  return LogLevel::NONE;
    return LogLevel::INFO;
}

// Core logging function.
// Writes a timestamped, multi-line-aware entry to the debugger output and,
// optionally, to <g_dir>/kashiwazaki.log.
void log_write(LogLevel lv, const std::string& msg) {
    if (lv < g_log_level) return;

    // Always mirror to the debugger output.
    OutputDebugStringA("[QuickShiori][");
    OutputDebugStringA(log_level_str(lv));
    OutputDebugStringA("] ");
    OutputDebugStringA(msg.c_str());
    OutputDebugStringA("\n");

    if (!g_log_to_file || g_dir.empty()) return;

    std::string path = g_dir + "/kashiwazaki.log";
    std::ofstream f(path, std::ios::app | std::ios::binary);
    if (!f) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    char ts[32];
    snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);

    // Write "[timestamp][LEVEL] " prefix on the first line, then indent
    // continuation lines so that JS stack traces remain readable.
    f << "[" << ts << "][" << log_level_str(lv) << "] ";
    for (size_t i = 0; i < msg.size(); ++i) {
        char c = msg[i];
        if (c == '\r') continue;          // strip bare CR
        if (c == '\n') {
            f << "\r\n";
            // Indent continuation lines to align under the message text.
            if (i + 1 < msg.size())
                f << "                       "; // 23 spaces
        } else {
            f << c;
        }
    }
    if (msg.empty() || msg.back() != '\n')
        f << "\r\n";
}

// ---------------------------------------------------------------------------
// JS exception helpers
// ---------------------------------------------------------------------------

// Pop the pending JS exception, return its formatted string, and clear it.
// Does NOT write to the log — caller decides what to do with the message.
static std::string collect_exception() {
    JSValue ex = JS_GetException(g_ctx);
    std::string msg;

    // Get the error message via toString() first (e.g. "TypeError: foo is not a function").
    // In QuickJS, .stack does NOT include the message line unlike V8.
    const char* str = JS_ToCString(g_ctx, ex);
    if (str) { msg = str; JS_FreeCString(g_ctx, str); }

    // Append the stack trace if present.
    JSValue stack = JS_GetPropertyStr(g_ctx, ex, "stack");
    if (!JS_IsUndefined(stack) && !JS_IsNull(stack)) {
        const char* s = JS_ToCString(g_ctx, stack);
        if (s && s[0] != '\0') {
            if (!msg.empty()) msg += '\n';
            msg += s;
            JS_FreeCString(g_ctx, s);
        }
    }
    JS_FreeValue(g_ctx, stack);
    JS_FreeValue(g_ctx, ex);
    return msg.empty() ? "(unknown JS exception)" : msg;
}

// Call a named global JS function with an optional single string argument.
// Returns {true, result_string} on success.
// Returns {false, error_message} if the function throws.
// Returns {true, ""} if the function does not exist or returns undefined/null.
static std::pair<bool, std::string> call_global(const char* name, const char* arg = nullptr) {
    JSValue global = JS_GetGlobalObject(g_ctx);
    JSValue fn = JS_GetPropertyStr(g_ctx, global, name);
    JS_FreeValue(g_ctx, global);

    if (!JS_IsFunction(g_ctx, fn)) {
        JS_FreeValue(g_ctx, fn);
        return {true, {}};
    }

    JSValue ret;
    if (arg != nullptr) {
        JSValue jsarg = JS_NewString(g_ctx, arg);
        ret = JS_Call(g_ctx, fn, JS_UNDEFINED, 1, &jsarg);
        JS_FreeValue(g_ctx, jsarg);
    } else {
        ret = JS_Call(g_ctx, fn, JS_UNDEFINED, 0, nullptr);
    }
    JS_FreeValue(g_ctx, fn);

    if (JS_IsException(ret)) {
        return {false, collect_exception()};
    }

    std::string result;
    if (!JS_IsUndefined(ret) && !JS_IsNull(ret)) {
        const char* s = JS_ToCString(g_ctx, ret);
        if (s) { result = s; JS_FreeCString(g_ctx, s); }
    }
    JS_FreeValue(g_ctx, ret);
    return {true, result};
}

// ---------------------------------------------------------------------------
// Node.js-style ES module resolution helpers
// ---------------------------------------------------------------------------

// Check whether a regular file exists at the given path.
static bool file_exists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// Try resolving `base` by appending common extensions and then /index variants.
// Candidates in order (mirrors Node.js LOAD_AS_FILE + LOAD_INDEX):
//   1. exact path
//   2. base + .js
//   3. base + .mjs
//   4. base/index.js
//   5. base/index.mjs
// Returns the first existing path, or empty string.
static std::string try_extensions(const std::string& base) {
    if (file_exists(base))              return base;
    for (const char* ext : {".js", ".mjs"}) {
        std::string p = base + ext;
        if (file_exists(p))             return p;
    }
    for (const char* idx : {"/index.js", "/index.mjs"}) {
        std::string p = base + idx;
        if (file_exists(p))             return p;
    }
    return {};
}

// Extract the value of a top-level string field from a JSON file.
// Lightweight, no full JSON parser — handles simple  "field": "value"  forms.
static std::string json_string_field(const std::string& path, const char* field) {
    std::string src;
    if (!read_file(path, src)) return {};

    std::string key = std::string("\"") + field + "\"";
    auto pos = src.find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();

    // Skip whitespace and ':'
    while (pos < src.size() && (src[pos]==' '||src[pos]=='\t'||src[pos]==':'||
                                 src[pos]=='\r'||src[pos]=='\n')) ++pos;
    if (pos >= src.size() || src[pos] != '"') return {};
    ++pos; // skip opening quote

    std::string val;
    while (pos < src.size() && src[pos] != '"') {
        if (src[pos] == '\\') { ++pos; }   // skip escape prefix
        if (pos < src.size()) val += src[pos++];
    }
    return val;
}

// Normalise path separators to forward-slash and collapse redundant segments.
// (Simple version: converts \ → /, does not handle .. traversal)
static std::string normalise_slashes(std::string p) {
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}

// Resolve a bare package specifier via the node_modules walk.
// Algorithm: LOAD_NODE_MODULES(name, start_dir)
//   For each ancestor directory D of start_dir (inclusive → root):
//     nm = D/node_modules/<name>
//     1. If nm/package.json exists:
//        a. Read "exports"["."] or "main" field → try_extensions(nm/<value>)
//        b. Fallback: try_extensions(nm)
//     2. Else try_extensions(nm)
// Returns absolute path or empty string.
static std::string resolve_node_modules(const std::string& start_dir,
                                         const std::string& name) {
    std::string dir = normalise_slashes(start_dir);

    while (true) {
        std::string nm = dir + "/node_modules/" + name;
        std::string pkg = nm + "/package.json";

        if (file_exists(pkg)) {
            // Try "exports": { ".": "<path>" } first (simple string value only)
            std::string exp = json_string_field(pkg, "exports");
            // exports could be complex — only use it when it's a plain string
            // (the field lookup stops at the first '"' so nested objects won't match)
            if (!exp.empty() && exp[0] != '{' && exp[0] != '[') {
                std::string r = try_extensions(nm + "/" + exp);
                if (!r.empty()) return r;
            }
            // Fall back to "main" field
            std::string main_val = json_string_field(pkg, "main");
            if (!main_val.empty()) {
                std::string r = try_extensions(nm + "/" + main_val);
                if (!r.empty()) return r;
                // main might already include the extension
                if (file_exists(nm + "/" + main_val))
                    return nm + "/" + main_val;
            }
            // package.json exists but no usable entry — try index
            std::string r = try_extensions(nm);
            if (!r.empty()) return r;
        } else {
            std::string r = try_extensions(nm);
            if (!r.empty()) return r;
        }

        // Walk up one directory level
        auto sep = dir.rfind('/');
        if (sep == std::string::npos || sep == 0) break;
        std::string parent = dir.substr(0, sep);
        if (parent == dir || parent.size() < 2) break;
        dir = parent;
    }
    return {};
}

// Return the directory portion of a file path (with trailing slash).
static std::string dir_of(const std::string& file_path) {
    auto s = file_path.find_last_of("/\\");
    return (s != std::string::npos) ? file_path.substr(0, s + 1) : g_dir + "/";
}

// ---------------------------------------------------------------------------
// Module loader
// ---------------------------------------------------------------------------

// Node.js-compatible module specifier resolver.
//
// Specifier categories (mirrors Node.js ESM resolve()):
//
//  1. Relative  ("./foo", "../bar"):
//       resolve relative to the importing file, then try_extensions().
//
//  2. Absolute  ("/foo", "C:/foo") or rooted with separator:
//       resolve from filesystem root as-is, then try_extensions().
//
//  3. Bare with known extension (.dll, .js, .mjs):
//       treat as path relative to g_dir (native/script file already in ghost).
//
//  4. Bare without extension — pure built-in names ("std", "os"):
//       return unchanged → QuickJS built-in registry handles them.
//
//  5. Bare name (no extension, but not a recognised built-in guard):
//       LOAD_NODE_MODULES walk starting from the importing file's directory.
//       If nothing found: fall back to g_dir/node_modules/<name>.
static char* node_module_normalizer(JSContext* ctx,
                                const char* base_name,
                                const char* module_name,
                                void* /*opaque*/) {
    std::string mod  = normalise_slashes(module_name);
    std::string base = normalise_slashes(base_name);

    // ---- 1 & 2: path-like specifiers ----------------------------------------
    bool is_relative = mod.rfind("./", 0) == 0 || mod.rfind("../", 0) == 0;
    bool is_absolute = mod.size() >= 1 && (mod[0] == '/') ||
                       (mod.size() >= 3 && mod[1] == ':' && mod[2] == '/'); // C:/...
    bool has_sep     = mod.find('/') != std::string::npos;

    if (is_relative || is_absolute) {
        std::string base_path = is_relative ? dir_of(base) + mod : mod;
        std::string r = try_extensions(base_path);
        if (!r.empty()) return js_strdup(ctx, r.c_str());
        // Return the unresolved path — module_loader will emit the proper error
        return js_strdup(ctx, base_path.c_str());
    }

    // ---- 3: bare specifier with explicit extension --------------------------
    auto dot     = mod.rfind('.');
    bool has_ext = (dot != std::string::npos && dot > mod.rfind('/'));

    if (has_ext || has_sep) {
        // Treat as path relative to the ghost root directory
        std::string r = try_extensions(g_dir + "/" + mod);
        if (!r.empty()) return js_strdup(ctx, r.c_str());
        return js_strdup(ctx, (g_dir + "/" + mod).c_str());
    }

    // ---- 4: recognised built-in bare names (QuickJS + quickshiori) ----------
    // These are pre-registered C modules; return unchanged so module_loader
    // finds them in the built-in registry rather than the file system.
    if (strcmp(module_name, "std")         == 0 ||
        strcmp(module_name, "os")          == 0 ||
        strcmp(module_name, "quickshiori") == 0)
        return js_strdup(ctx, module_name);

    // ---- 5: bare package name — node_modules walk ---------------------------
    // Start searching from the directory of the importing file.
    std::string search_start = (base.find('/') != std::string::npos)
        ? base.substr(0, base.rfind('/'))   // dirname of importer
        : g_dir;

    std::string r = resolve_node_modules(search_start, mod);
    if (!r.empty()) return js_strdup(ctx, r.c_str());

    // Specifier completely unresolved — return as-is and let module_loader
    // throw a meaningful ReferenceError.
    return js_strdup(ctx, module_name);
}

// ---------------------------------------------------------------------------
// Init / teardown
// ---------------------------------------------------------------------------

static bool do_init(std::string dir_utf8) {
    // Normalize: strip trailing slashes
    while (!dir_utf8.empty() && (dir_utf8.back() == '/' || dir_utf8.back() == '\\'))
        dir_utf8.pop_back();
    g_dir = dir_utf8;

    log_info("Initializing... dir=" + g_dir);

    g_rt = JS_NewRuntime();
    if (!g_rt) {
        log_error("Failed to create runtime");
        return false;
    }
    log_debug("Runtime created");

    // Initialize std handlers for the runtime (required for std/os modules)
    js_std_init_handlers(g_rt);
    log_debug("Std handlers initialized");

    g_ctx = JS_NewContext(g_rt);
    if (!g_ctx) {
        log_error("Failed to create context");
        js_std_free_handlers(g_rt);
        JS_FreeRuntime(g_rt); g_rt = nullptr;
        return false;
    }
    log_debug("Context created");

    // Add standard helpers (console.log, etc.)
    js_std_add_helpers(g_ctx, 0, nullptr);
    log_debug("Std helpers added");

    // Set module loader
    log_debug("Setting module loader...");
    JS_SetModuleLoaderFunc2(g_rt, node_module_normalizer, js_module_loader, js_module_check_attributes ,nullptr);
    log_debug("Module loader set");

    // Load entry point: index.js as an ES module so that top-level `import`
    // statements work and the registered module loader is invoked.
    // index.js should expose the three hook functions via globalThis.
    std::string src;
    std::string entry = g_dir + "/index.js";
    log_info("Loading entry point: " + entry);
    if (!read_file(entry, src)) {
        log_error("Cannot open entry point: " + entry);
        JS_FreeContext(g_ctx); g_ctx = nullptr;
        js_std_free_handlers(g_rt);
        JS_FreeRuntime(g_rt);  g_rt  = nullptr;
        return false;
    }
    log_debug("Entry point loaded, size=" + std::to_string(src.size()));
    // Set __shiori_dir in globalThis before executing index.js.
    // JS_SetPropertyStr steals the reference to the value — do NOT JS_FreeValue it.
    {
        JSValue global = JS_GetGlobalObject(g_ctx);
        JS_DefinePropertyValueStr(g_ctx, global, "__shiori_dir", 
                          JS_NewString(g_ctx, g_dir.c_str()), 
                          JS_PROP_ENUMERABLE | JS_PROP_HAS_VALUE);
        JS_FreeValue(g_ctx, global);
    }

    // Register built-in "quickshiori" ES module.
    js_init_module_quickshiori(g_ctx, "quickshiori");
    log_debug("\"quickshiori\" module registered");

    // Phase 1: compile
    log_debug("Compiling index.js...");
    JSValue func_val = JS_Eval(g_ctx, src.c_str(), src.size(),
                               entry.c_str(),
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(func_val)) {
        std::string err = collect_exception();
        log_error("Failed to compile index.js:\n" + err);
        JS_FreeValue(g_ctx, func_val);
        JS_FreeContext(g_ctx); g_ctx = nullptr;
        js_std_free_handlers(g_rt);
        JS_FreeRuntime(g_rt);  g_rt  = nullptr;
        return false;
    }
    log_debug("Compilation successful");



    // Phase 2: execute — JS_EvalFunction takes ownership of func_val
    log_debug("Executing index.js...");
    JSValue result = JS_EvalFunction(g_ctx, func_val);
    if (JS_IsException(result)) {
        std::string err = collect_exception();
        log_error("Failed to execute index.js:\n" + err);
        JS_FreeValue(g_ctx, result);
        JS_FreeContext(g_ctx); g_ctx = nullptr;
        js_std_free_handlers(g_rt);
        JS_FreeRuntime(g_rt);  g_rt  = nullptr;
        return false;
    }
    JS_FreeValue(g_ctx, result);
    log_info("Initialization complete");
    return true;
}

static void do_teardown() {
    if (g_ctx) { JS_FreeContext(g_ctx); g_ctx = nullptr; }
    if (g_rt) {
        js_std_free_handlers(g_rt);
        JS_FreeRuntime(g_rt);
        g_rt = nullptr;
    }
    g_initialized = false;
}

// ---------------------------------------------------------------------------
// DLL exports  (spec_dll.html)
// ---------------------------------------------------------------------------

// Shared logic for loadu/load after path is resolved to UTF-8.
static BOOL do_load(std::string path_utf8) {
    log_info("do_load called with: " + path_utf8);
    if (!do_init(path_utf8)) {
        log_error("do_init failed");
        return FALSE;
    }
    g_initialized = true;

    log_debug("Calling __shiori_load...");
    auto [ok, err] = call_global("__shiori_load", g_dir.c_str());
    if (!ok) {
        log_error("__shiori_load threw: " + err);
        do_teardown();
        return FALSE;
    }
    log_debug("__shiori_load succeeded");
    return TRUE;
}

// loadu: called preferentially by newer base ware (SSP >= 2.6.92).
//        Path is UTF-8.
extern "C" __declspec(dllexport) BOOL __cdecl loadu(HGLOBAL h, long len) {
    OutputDebugStringA("[QuickShioriRuntime] loadu called\n");
    if (g_initialized) {
        OutputDebugStringA("[QuickShioriRuntime] Already initialized\n");
        GlobalFree(h);
        return TRUE;
    }
    std::string path(reinterpret_cast<char*>(h), len);
    GlobalFree(h);
    BOOL result = do_load(std::move(path));
    OutputDebugStringA(result ? "[QuickShioriRuntime] loadu succeeded\n" : "[QuickShioriRuntime] loadu failed\n");
    return result;
}

// load: legacy fallback; path is CP_ACP (ANSI codepage).
extern "C" __declspec(dllexport) BOOL __cdecl load(HGLOBAL h, long len) {
    OutputDebugStringA("[QuickShioriRuntime] load called\n");
    if (g_initialized) { GlobalFree(h); return TRUE; }
    std::string path = cp_oemcp_to_utf8(reinterpret_cast<char*>(h), len);
    GlobalFree(h);
    return do_load(std::move(path));
}

// Build a SHIORI/3.0 500 Internal Server Error response.
// error_desc must not contain CR or LF (caller should sanitize).
static std::string make_500(const std::string& error_desc) {
    // Strip CR/LF from the description so it doesn't break the header line.
    std::string safe;
    safe.reserve(error_desc.size());
    for (char c : error_desc)
        if (c != '\r' && c != '\n') safe += c;
        else safe += ' ';

    return "SHIORI/3.0 500 Internal Server Error\r\n"
           "Charset: UTF-8\r\n"
           "Sender: QuickShioriRuntime\r\n"
           "ErrorLevel: critical\r\n"
           "ErrorDescription: " + safe + "\r\n"
           "\r\n";
}

// request: single entry point for all SHIORI/SAORI/PLUGIN events.
//          Raw request bytes are forwarded to JS as a string.
//          JS must return the complete response string
//          (status line + headers + body).
extern "C" __declspec(dllexport) HGLOBAL __cdecl request(HGLOBAL h, long* len) {
    if (!h || !len || !g_ctx) return nullptr;

    std::string req(reinterpret_cast<char*>(h), *len);
    GlobalFree(h);
    auto [ok, resp] = call_global("__shiori_request", req.c_str());
    if (!ok) {
        log_error("__shiori_request threw: " + resp);
        return to_hglobal(make_500(resp), len);
    }
    js_std_loop_once(g_ctx); // Process pending jobs before handling the request
    if (resp.empty()) return nullptr;
    return to_hglobal(resp, len);
}

// unload: clean up everything.
extern "C" __declspec(dllexport) BOOL __cdecl unload() {
    if (g_ctx) {
        auto [ok, err] = call_global("__shiori_unload");
        if (!ok) {
            log_error("__shiori_unload threw: " + err);
            do_teardown();
            return FALSE;
        }
    }
    do_teardown();
    return TRUE;
}

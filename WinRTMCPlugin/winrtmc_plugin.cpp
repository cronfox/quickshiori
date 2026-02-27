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

/*
 * winrtmc_plugin.cpp
 *
 * WinRTMCPlugin – セルフコンテインド SHIORI DLL
 *
 * 静的リンク構成：
 *   - QuickJS-NG (qjs + qjs-libc)       : JS エンジン本体
 *   - quickshiori_module (quickshiori)  : ログ・プロセス情報 JS モジュール
 *   - quickjs_winrtmc    (winrtmc)      : Windows メディアセッション JS モジュール
 *   - entry.js                          : ビルド時に qjsc でコンパイルした内蔵バイトコード
 *
 * quickshiori.cpp から分岐した実装。主な相違点:
 *   1. index.js をディスクから読むのではなく JS_EvalBinary() で内蔵バイトコードを実行する。
 *   2. QuickJS コンテキスト初期化時に winrtmc モジュールも登録する。
 *   3. node_module_normalizer に "winrtmc" を組み込みモジュールとして追加。
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
#include "quickjs_winrtmc.h"

// ---------------------------------------------------------------------------
// 内蔵バイトコード（qjsc によってビルド時に entry.js から生成）
// entry_bytecode.c は add_custom_command で生成され、このターゲットの
// INCLUDE_DIRECTORIES で見える場所に置かれます。
// ---------------------------------------------------------------------------
extern "C" {
    extern const uint32_t qjsc_entry_size;
    extern const uint8_t  qjsc_entry[];
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static JSRuntime* g_rt  = nullptr;
static JSContext* g_ctx = nullptr;
static std::string g_dir;
static bool g_initialized = false;

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

static std::string cp_oemcp_to_utf8(const char* src, int len) {
    int wlen = MultiByteToWideChar(CP_OEMCP, 0, src, len, nullptr, 0);
    std::vector<wchar_t> wbuf(wlen);
    MultiByteToWideChar(CP_OEMCP, 0, src, len, wbuf.data(), wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, nullptr, 0, nullptr, nullptr);
    std::vector<char> ubuf(ulen);
    WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, ubuf.data(), ulen, nullptr, nullptr);
    return {ubuf.begin(), ubuf.end()};
}

static HGLOBAL to_hglobal(const std::string& s, long* len) {
    *len = static_cast<long>(s.size());
    HGLOBAL h = GlobalAlloc(GMEM_FIXED, *len + 1);
    if (h) {
        memcpy(reinterpret_cast<char*>(h), s.data(), *len);
        reinterpret_cast<char*>(h)[*len] = '\0';
    }
    return h;
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

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

void log_write(LogLevel lv, const std::string& msg) {
    if (lv < g_log_level) return;

    OutputDebugStringA("[WinRTMCPlugin][");
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

    f << "[" << ts << "][" << log_level_str(lv) << "] ";
    for (size_t i = 0; i < msg.size(); ++i) {
        char c = msg[i];
        if (c == '\r') continue;
        if (c == '\n') {
            f << "\r\n";
            if (i + 1 < msg.size())
                f << "                       ";
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

static std::string collect_exception() {
    JSValue ex = JS_GetException(g_ctx);
    std::string msg;

    const char* str = JS_ToCString(g_ctx, ex);
    if (str) { msg = str; JS_FreeCString(g_ctx, str); }

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
// Node.js スタイルモジュール解決
// ---------------------------------------------------------------------------

static bool file_exists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

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

static std::string json_string_field(const std::string& path, const char* field) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss; ss << f.rdbuf();
    std::string src = ss.str();

    std::string key = std::string("\"") + field + "\"";
    auto pos = src.find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();
    while (pos < src.size() && (src[pos]==' '||src[pos]=='\t'||src[pos]==':'||
                                 src[pos]=='\r'||src[pos]=='\n')) ++pos;
    if (pos >= src.size() || src[pos] != '"') return {};
    ++pos;
    std::string val;
    while (pos < src.size() && src[pos] != '"') {
        if (src[pos] == '\\') { ++pos; }
        if (pos < src.size()) val += src[pos++];
    }
    return val;
}

static std::string normalise_slashes(std::string p) {
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}

static std::string resolve_node_modules(const std::string& start_dir,
                                         const std::string& name) {
    std::string dir = normalise_slashes(start_dir);
    while (true) {
        std::string nm  = dir + "/node_modules/" + name;
        std::string pkg = nm + "/package.json";
        if (file_exists(pkg)) {
            std::string exp = json_string_field(pkg, "exports");
            if (!exp.empty() && exp[0] != '{' && exp[0] != '[') {
                std::string r = try_extensions(nm + "/" + exp);
                if (!r.empty()) return r;
            }
            std::string main_val = json_string_field(pkg, "main");
            if (!main_val.empty()) {
                std::string r = try_extensions(nm + "/" + main_val);
                if (!r.empty()) return r;
            }
            std::string r = try_extensions(nm);
            if (!r.empty()) return r;
        } else {
            std::string r = try_extensions(nm);
            if (!r.empty()) return r;
        }
        auto sep = dir.rfind('/');
        if (sep == std::string::npos || sep == 0) break;
        std::string parent = dir.substr(0, sep);
        if (parent == dir || parent.size() < 2) break;
        dir = parent;
    }
    return {};
}

static std::string dir_of(const std::string& file_path) {
    auto s = file_path.find_last_of("/\\");
    return (s != std::string::npos) ? file_path.substr(0, s + 1) : g_dir + "/";
}

// ---------------------------------------------------------------------------
// モジュールローダー
// ---------------------------------------------------------------------------

static char* node_module_normalizer(JSContext* ctx,
                                const char* base_name,
                                const char* module_name,
                                void* /*opaque*/) {
    std::string mod  = normalise_slashes(module_name);
    std::string base = normalise_slashes(base_name);

    bool is_relative = mod.rfind("./", 0) == 0 || mod.rfind("../", 0) == 0;
    bool is_absolute = mod.size() >= 1 && (mod[0] == '/') ||
                       (mod.size() >= 3 && mod[1] == ':' && mod[2] == '/');

    if (is_relative || is_absolute) {
        std::string base_path = is_relative ? dir_of(base) + mod : mod;
        std::string r = try_extensions(base_path);
        if (!r.empty()) return js_strdup(ctx, r.c_str());
        return js_strdup(ctx, base_path.c_str());
    }

    auto dot     = mod.rfind('.');
    bool has_ext = (dot != std::string::npos && dot > mod.rfind('/'));
    bool has_sep = mod.find('/') != std::string::npos;

    if (has_ext || has_sep) {
        std::string r = try_extensions(g_dir + "/" + mod);
        if (!r.empty()) return js_strdup(ctx, r.c_str());
        return js_strdup(ctx, (g_dir + "/" + mod).c_str());
    }

    // ── 組み込み C モジュール名（ディスクを検索しない）────────────────────────
    if (strcmp(module_name, "std")         == 0 ||
        strcmp(module_name, "os")          == 0 ||
        strcmp(module_name, "quickshiori") == 0 ||
        strcmp(module_name, "winrtmc")     == 0)   // ← winrtmc は静的組み込み
        return js_strdup(ctx, module_name);

    std::string search_start = (base.find('/') != std::string::npos)
        ? base.substr(0, base.rfind('/'))
        : g_dir;

    std::string r = resolve_node_modules(search_start, mod);
    if (!r.empty()) return js_strdup(ctx, r.c_str());
    return js_strdup(ctx, module_name);
}

// ---------------------------------------------------------------------------
// Init / teardown
// ---------------------------------------------------------------------------

static void do_teardown();

static bool do_init(std::string dir_utf8) {
    while (!dir_utf8.empty() && (dir_utf8.back() == '/' || dir_utf8.back() == '\\'))
        dir_utf8.pop_back();
    g_dir = dir_utf8;

    log_info("Initializing... dir=" + g_dir);

    g_rt = JS_NewRuntime();
    if (!g_rt) { log_error("Failed to create runtime"); return false; }

    js_std_init_handlers(g_rt);

    g_ctx = JS_NewContext(g_rt);
    if (!g_ctx) {
        log_error("Failed to create context");
        js_std_free_handlers(g_rt);
        JS_FreeRuntime(g_rt); g_rt = nullptr;
        return false;
    }

    js_std_add_helpers(g_ctx, 0, nullptr);

    JS_SetModuleLoaderFunc2(g_rt, node_module_normalizer,
                            js_module_loader, js_module_check_attributes, nullptr);

    // ── __shiori_dir をグローバルに設定 ────────────────────────────────────
    {
        JSValue global = JS_GetGlobalObject(g_ctx);
        JS_DefinePropertyValueStr(g_ctx, global, "__shiori_dir",
                          JS_NewString(g_ctx, g_dir.c_str()),
                          JS_PROP_ENUMERABLE | JS_PROP_HAS_VALUE);
        JS_FreeValue(g_ctx, global);
    }

    // ── C モジュール登録 ────────────────────────────────────────────────────
    js_init_module_quickshiori(g_ctx, "quickshiori");
    log_debug("\"quickshiori\" module registered");

    js_init_module_winrtmc(g_ctx);          // winrtmc を静的組み込みとして登録
    log_debug("\"winrtmc\" module registered (static)");

    // ── 内蔵バイトコードを評価 ──────────────────────────────────────────────
    // qjsc -c -m entry.js が生成した qjsc_entry[] / qjsc_entry_size を使用。
    log_info("Evaluating embedded bytecode (entry.js)...");

    JSValue func_val = JS_ReadObject(g_ctx, qjsc_entry, qjsc_entry_size,
                                     JS_READ_OBJ_BYTECODE);
    if (JS_IsException(func_val)) {
        log_error("Failed to deserialize bytecode:\n" + collect_exception());
        do_teardown();
        return false;
    }

    JSValue result = JS_EvalFunction(g_ctx, func_val); // func_val の所有権を移譲
    if (JS_IsException(result)) {
        log_error("Failed to evaluate embedded bytecode:\n" + collect_exception());
        JS_FreeValue(g_ctx, result);
        do_teardown();
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
// DLL exports  (SHIORI 仕様)
// ---------------------------------------------------------------------------

static BOOL do_load(std::string path_utf8) {
    log_info("do_load: " + path_utf8);
    if (!do_init(path_utf8)) { log_error("do_init failed"); return FALSE; }
    g_initialized = true;

    auto [ok, err] = call_global("__shiori_load", g_dir.c_str());
    if (!ok) {
        log_error("__shiori_load threw: " + err);
        do_teardown();
        return FALSE;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL __cdecl loadu(HGLOBAL h, long len) {
    OutputDebugStringA("[WinRTMCPlugin] loadu called\n");
    if (g_initialized) { GlobalFree(h); return TRUE; }
    std::string path(reinterpret_cast<char*>(h), len);
    GlobalFree(h);
    BOOL result = do_load(std::move(path));
    OutputDebugStringA(result ? "[WinRTMCPlugin] loadu succeeded\n"
                              : "[WinRTMCPlugin] loadu failed\n");
    return result;
}

extern "C" __declspec(dllexport) BOOL __cdecl load(HGLOBAL h, long len) {
    OutputDebugStringA("[WinRTMCPlugin] load called\n");
    if (g_initialized) { GlobalFree(h); return TRUE; }
    std::string path = cp_oemcp_to_utf8(reinterpret_cast<char*>(h), len);
    GlobalFree(h);
    return do_load(std::move(path));
}

static std::string make_500(const std::string& error_desc) {
    std::string safe;
    safe.reserve(error_desc.size());
    for (char c : error_desc)
        if (c != '\r' && c != '\n') safe += c; else safe += ' ';
    return "SHIORI/3.0 500 Internal Server Error\r\n"
           "Charset: UTF-8\r\n"
           "Sender: WinRTMCPlugin\r\n"
           "ErrorLevel: critical\r\n"
           "ErrorDescription: " + safe + "\r\n"
           "\r\n";
}

extern "C" __declspec(dllexport) HGLOBAL __cdecl request(HGLOBAL h, long* len) {
    if (!h || !len || !g_ctx) return nullptr;
    std::string req(reinterpret_cast<char*>(h), *len);
    GlobalFree(h);
    js_std_loop_once(g_ctx);
    auto [ok, resp] = call_global("__shiori_request", req.c_str());
    if (!ok) {
        log_error("__shiori_request threw: " + resp);
        return to_hglobal(make_500(resp), len);
    }
    if (resp.empty()) return nullptr;
    return to_hglobal(resp, len);
}

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

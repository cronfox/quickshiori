/*
 * Copyright (c) 2026 Cronfox
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

extern "C" {
#include "quickjs.h"
}

// ---------------------------------------------------------------------------
// Internal: CP_ACP → UTF-8 conversion
// ---------------------------------------------------------------------------

static std::string cp_acp_to_utf8(const char* src, int len) {
    if (!src || len == 0) return {};
    int wlen = MultiByteToWideChar(CP_ACP, 0, src, len, nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring wide(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, src, len, wide.data(), wlen);
    int u8len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return {};
    std::string result(u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wlen, result.data(), u8len, nullptr, nullptr);
    return result;
}

// ---------------------------------------------------------------------------
// Internal: FMO line parser
// ---------------------------------------------------------------------------
// Format: {32-byte-id}.{key}\x01{value}\r\n

struct FmoLine {
    std::string id;
    std::string key;
    std::string value;
};

static bool parse_fmo_line(const char* line, size_t len, FmoLine* out) {
    const char* dot = nullptr;
    const char* delim = nullptr;

    for (const char* p = line; p < line + len; ++p) {
        if (*p == '.' && !dot) {
            dot = p;
        } else if (*p == '\x01' && !delim) {
            delim = p;
        }
    }

    if (!dot || !delim || delim < dot) return false;

    out->id.assign(line, dot - line);
    out->key.assign(dot + 1, delim - dot - 1);
    out->value.assign(delim + 1, line + len - delim - 1);
    return true;
}

// ---------------------------------------------------------------------------
// Internal: Read raw FMO data
// ---------------------------------------------------------------------------
// Tries SakuraUnicode (UTF-8) first, then Sakura (CP_ACP → UTF-8).
// Acquires FMO Mutex for exclusive read; silently skips on failure.

struct RawFmo {
    std::string name;
    std::string data;
};

static bool read_fmo_impl(RawFmo* out) {
    static const wchar_t* const mutex_names[] = { L"SakuraUnicodeFMO", L"SakuraFMO" };
    static const wchar_t* const fmo_names[]    = { L"SakuraUnicode",   L"Sakura" };
    static const bool is_utf8[]                = { true,               false };

    for (int i = 0; i < 2; ++i) {
        HANDLE hMutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, mutex_names[i]);
        if (!hMutex) {
            continue;
        }

        DWORD wait_result = WaitForSingleObject(hMutex, 100);
        bool locked = (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_ABANDONED);
        if (!locked) {
            CloseHandle(hMutex);
            continue;
        }

        HANDLE hFmo = OpenFileMappingW(FILE_MAP_READ, FALSE, fmo_names[i]);
        if (!hFmo) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            continue;
        }

        char* raw = static_cast<char*>(MapViewOfFile(hFmo, FILE_MAP_READ, 0, 0, 0));
        if (!raw) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            CloseHandle(hFmo);
            continue;
        }

        MEMORY_BASIC_INFORMATION mbi{};
        SIZE_T queried = VirtualQuery(raw, &mbi, sizeof(mbi));
        if (queried != sizeof(mbi) || mbi.RegionSize < sizeof(uint32_t)) {
            UnmapViewOfFile(raw);
            CloseHandle(hFmo);
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            continue;
        }

        uint32_t header_len = *reinterpret_cast<const uint32_t*>(raw);
        size_t max_data_len = static_cast<size_t>(mbi.RegionSize) - sizeof(uint32_t);
        const char* data = raw + sizeof(uint32_t);

        size_t content_len = 0;
        if (header_len <= max_data_len) {
            content_len = static_cast<size_t>(header_len);
        } else {
            const void* nul_pos = std::memchr(data, '\0', max_data_len);
            content_len = nul_pos ? static_cast<size_t>(static_cast<const char*>(nul_pos) - data)
                                  : max_data_len;
        }

        std::string content(data, content_len);
        if (is_utf8[i]) {
            out->data = std::move(content);
        } else {
            out->data = cp_acp_to_utf8(content.c_str(), static_cast<int>(content.size()));
        }

        char name_buf[32];
        WideCharToMultiByte(CP_UTF8, 0, fmo_names[i], -1, name_buf, sizeof(name_buf), nullptr, nullptr);
        out->name = name_buf;

        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        UnmapViewOfFile(raw);
        CloseHandle(hFmo);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Internal: Parse raw FMO data into structured entries
// ---------------------------------------------------------------------------

struct FmoEntry {
    std::string id;
    std::unordered_map<std::string, std::string> fields;
};

static std::vector<FmoEntry> parse_fmo_data(const std::string& data) {
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> map;

    const char* p = data.data();
    const char* end = data.data() + data.size();

    while (p < end) {
        const char* line_start = p;
        while (p < end && *p != '\r' && *p != '\n') ++p;

        size_t line_len = p - line_start;
        if (line_len > 0) {
            FmoLine line;
            if (parse_fmo_line(line_start, line_len, &line)) {
                map[line.id][line.key] = std::move(line.value);
            }
        }

        if (p < end && *p == '\r') ++p;
        if (p < end && *p == '\n') ++p;
    }

    std::vector<FmoEntry> result;
    result.reserve(map.size());
    for (auto& [id, fields] : map) {
        FmoEntry entry;
        entry.id = std::move(id);
        entry.fields = std::move(fields);
        result.push_back(std::move(entry));
    }
    return result;
}

// ---------------------------------------------------------------------------
// JS: isRunning
// ---------------------------------------------------------------------------
// SSP holds mutex "ssp", Materia/CROW hold "sakura".
// Either one indicates baseware is running.

static JSValue js_is_running(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    HANDLE h1 = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, L"ssp");
    HANDLE h2 = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, L"sakura");

    bool running = (h1 != nullptr) || (h2 != nullptr);

    if (h1) CloseHandle(h1);
    if (h2) CloseHandle(h2);

    return JS_NewBool(ctx, running);
}

// ---------------------------------------------------------------------------
// JS: readFMO
// ---------------------------------------------------------------------------
// Returns array of objects, each with "id" and all FMO key-value pairs.

static JSValue js_read_fmo(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    RawFmo raw;
    if (!read_fmo_impl(&raw)) {
        return JS_NewArray(ctx);
    }

    auto entries = parse_fmo_data(raw.data);

    JSValue arr = JS_NewArray(ctx);
    uint32_t idx = 0;
    for (auto& entry : entries) {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "id", JS_NewStringLen(ctx, entry.id.c_str(), entry.id.size()));
        for (auto& [key, value] : entry.fields) {
            JS_SetPropertyStr(ctx, obj, key.c_str(), JS_NewStringLen(ctx, value.c_str(), value.size()));
        }
        JS_SetPropertyUint32(ctx, arr, idx++, obj);
    }
    return arr;
}

// ---------------------------------------------------------------------------
// JS: readFMORaw
// ---------------------------------------------------------------------------
// Returns raw FMO data as UTF-8 string, or null if not found.
// Tries SakuraUnicode first, then Sakura.

static JSValue js_read_fmo_raw(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    RawFmo raw;
    if (!read_fmo_impl(&raw)) {
        return JS_NULL;
    }
    return JS_NewStringLen(ctx, raw.data.data(), raw.data.size());
}

// ---------------------------------------------------------------------------
// Module Definition
// ---------------------------------------------------------------------------

static const JSCFunctionListEntry ukafmo_funcs[] = {
    JS_CFUNC_DEF("isRunning",  0, js_is_running),
    JS_CFUNC_DEF("readFMO",    0, js_read_fmo),
    JS_CFUNC_DEF("readFMORaw", 0, js_read_fmo_raw),
};

static int js_ukafmo_module_init(JSContext* ctx, JSModuleDef* m) {
    return JS_SetModuleExportList(ctx, m, ukafmo_funcs,
                                  sizeof(ukafmo_funcs) / sizeof(ukafmo_funcs[0]));
}

extern "C" __declspec(dllexport) JSModuleDef* js_init_module(JSContext* ctx, const char* module_name) {
    JSModuleDef* m = JS_NewCModule(ctx, module_name, js_ukafmo_module_init);
    if (!m) return nullptr;
    JS_AddModuleExportList(ctx, m, ukafmo_funcs,
                           sizeof(ukafmo_funcs) / sizeof(ukafmo_funcs[0]));
    return m;
}

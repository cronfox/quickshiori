/*
 * Copyright (c) 2026 Cronfox
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * quickshiori_module.cpp
 *
 * Implements the built-in "quickshiori" ES module that is compiled directly
 * into quickshiori.dll.  Any JS file can use it without a separate DLL:
 *
 *   import { log, debug, info, warn, error, setLevel, setFileOutput }
 *     from "quickshiori";
 *
 * Exports
 * -------
 *   log(level, ...args)
 *       Write a log entry at an explicit level.
 *       level: "debug" | "info" | "warn" | "error"  (case-insensitive)
 *       args:  joined with spaces, identical to console.log semantics.
 *
 *   debug(...args)  / info(...args)  / warn(...args)  / error(...args)
 *       Shorthand wrappers that hard-code the log level.
 *
 *   setLevel(level)
 *       Change the minimum recorded level at runtime.
 *       Accepts the same strings as the first argument of log().
 *       Returns the previous level string.
 *
 *   setFileOutput(enabled)
 *       Pass true/false to enable or disable writing to kashiwazaki.log.
 *       Returns the previous boolean value.
 */

#include "quickshiori_log.h"
#include "quickshiori_module.h"
#include "quickjs.h"
#include "version.h"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Join JS arguments [start, argc) into a space-separated string using the
// same stringification rules as console.log (JS_ToString on each value).
static std::string js_args_to_string(JSContext* ctx, int argc, JSValue* argv, int start) {
    std::string result;
    for (int i = start; i < argc; ++i) {
        if (i > start) result += ' ';
        JSValue sv = JS_ToString(ctx, argv[i]);
        const char* s = JS_ToCString(ctx, sv);
        if (s) { result += s; JS_FreeCString(ctx, s); }
        JS_FreeValue(ctx, sv);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Exported functions
// ---------------------------------------------------------------------------

// log(level, ...args)
static JSValue js_qs_log(JSContext* ctx, JSValue /*this*/,
                          int argc, JSValue* argv) {
    LogLevel lv = LogLevel::INFO;
    if (argc >= 1) {
        const char* ls = JS_ToCString(ctx, argv[0]);
        if (ls) { lv = log_level_from_str(ls); JS_FreeCString(ctx, ls); }
    }
    log_write(lv, js_args_to_string(ctx, argc, argv, 1));
    return JS_UNDEFINED;
}

// debug(...args)
static JSValue js_qs_debug(JSContext* ctx, JSValue /*this*/,
                            int argc, JSValue* argv) {
    log_write(LogLevel::DEBUG, js_args_to_string(ctx, argc, argv, 0));
    return JS_UNDEFINED;
}

// info(...args)
static JSValue js_qs_info(JSContext* ctx, JSValue /*this*/,
                           int argc, JSValue* argv) {
    log_write(LogLevel::INFO, js_args_to_string(ctx, argc, argv, 0));
    return JS_UNDEFINED;
}

// warn(...args)
static JSValue js_qs_warn(JSContext* ctx, JSValue /*this*/,
                           int argc, JSValue* argv) {
    log_write(LogLevel::WARN, js_args_to_string(ctx, argc, argv, 0));
    return JS_UNDEFINED;
}

// error(...args)
static JSValue js_qs_error(JSContext* ctx, JSValue /*this*/,
                            int argc, JSValue* argv) {
    log_write(LogLevel::ERR, js_args_to_string(ctx, argc, argv, 0));
    return JS_UNDEFINED;
}

// setLevel(levelStr) — returns the previous level string
static JSValue js_qs_set_level(JSContext* ctx, JSValue /*this*/,
                                int argc, JSValue* argv) {
    const char* prev = log_level_str(g_log_level);
    if (argc >= 1) {
        const char* ls = JS_ToCString(ctx, argv[0]);
        if (ls) {
            g_log_level = log_level_from_str(ls);
            JS_FreeCString(ctx, ls);
        }
    }
    return JS_NewString(ctx, prev);
}

// setFileOutput(bool) — returns the previous boolean value
static JSValue js_qs_set_file_output(JSContext* ctx, JSValue /*this*/,
                                      int argc, JSValue* argv) {
    bool prev = g_log_to_file;
    if (argc >= 1)
        g_log_to_file = JS_ToBool(ctx, argv[0]) != 0;
    return JS_NewBool(ctx, prev);
}

// ---------------------------------------------------------------------------
// Module registration
// ---------------------------------------------------------------------------

// Function-based exports (registered via JSCFunctionListEntry).
static const JSCFunctionListEntry qs_module_funcs[] = {
    JS_CFUNC_DEF("log",           1, js_qs_log),
    JS_CFUNC_DEF("debug",         0, js_qs_debug),
    JS_CFUNC_DEF("info",          0, js_qs_info),
    JS_CFUNC_DEF("warn",          0, js_qs_warn),
    JS_CFUNC_DEF("error",         0, js_qs_error),
    JS_CFUNC_DEF("setLevel",      1, js_qs_set_level),
    JS_CFUNC_DEF("setFileOutput", 1, js_qs_set_file_output),
};

// Module initializer — called by QuickJS when the module is first imported.
static int js_quickshiori_module_init(JSContext* ctx, JSModuleDef* m) {
    // ---- function exports --------------------------------------------------
    if (JS_SetModuleExportList(ctx, m,
                               qs_module_funcs,
                               sizeof(qs_module_funcs) / sizeof(qs_module_funcs[0])) != 0)
        return -1;

    // ---- process : { version, versions } ----------------------------------
    // process.version  : string  e.g. "0.0.1"
    // process.versions : { quickshiori, "quickjs-ng" }
    JSValue versions = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, versions, "quickshiori",
                      JS_NewString(ctx, QUICKSHIORI_VERSION));
    JS_SetPropertyStr(ctx, versions, "quickjs-ng",
                      JS_NewString(ctx, JS_GetVersion()));

    JSValue process = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, process, "version",
                      JS_NewString(ctx, QUICKSHIORI_VERSION));
    JS_SetPropertyStr(ctx, process, "versions", versions);

    if (JS_SetModuleExport(ctx, m, "process", process) != 0)
        return -1;

    return 0;
}

JSModuleDef* js_init_module_quickshiori(JSContext* ctx, const char* module_name) {
    JSModuleDef* m = JS_NewCModule(ctx, module_name, js_quickshiori_module_init);
    if (!m) return nullptr;
    // Register all named exports (functions + value slots).
    JS_AddModuleExportList(ctx, m,
                           qs_module_funcs,
                           sizeof(qs_module_funcs) / sizeof(qs_module_funcs[0]));
    JS_AddModuleExport(ctx, m, "process");
    return m;
}

/*
 * qjs_reexport.c
 *
 * PURPOSE
 * -------
 * quickshiori.dll statically links qjs (QuickJS-ng) and re-exports the entire
 * QuickJS public API through its own export table.
 *
 * This file achieves that by compiling a translation unit that includes
 * quickjs.h with BUILDING_QJS_SHARED defined.  BUILDING_QJS_SHARED causes every
 * JS_EXTERN declaration to become __declspec(dllexport).  The MSVC linker then
 * sees the corresponding EXPDEF records and exports those symbols from
 * quickshiori.dll, even though their implementations live in the statically-
 * linked qjs.lib.
 *
 * EFFECT ON NATIVE MODULE AUTHORS
 * --------------------------------
 * Third-party QuickShiori native modules (.dll) must:
 *
 *   1. Include quickjs.h **after** defining USING_QJS_SHARED:
 *        #define USING_QJS_SHARED
 *        #include "quickjs.h"
 *      This turns every JS_EXTERN into __declspec(dllimport), telling the
 *      compiler to call through the import thunks in quickshiori.lib.
 *
 *   2. Link against quickshiori.lib (the import library), NOT qjs.lib.
 *
 *   3. Do NOT link qjs.lib or any other copy of QuickJS.
 *
 * With this setup there is exactly ONE QuickJS runtime in the process (the one
 * inside quickshiori.dll).  JSContext* pointers, JSValue tokens, and
 * JSClassID values are all coherent across the host and every native module
 * regardless of when the module was compiled.
 *
 * VERSION COMPATIBILITY
 * ----------------------
 * A native module compiled against QuickJS-ng vX.Y will work with any future
 * quickshiori.dll that ships the same or a compatible QuickJS-ng ABI.
 * QuickJS-ng uses semantic versioning; minor/patch bumps preserve the ABI.
 * A major bump would require recompiling the module, but crucially the module
 * author only needs to recompile, not touch any QuickShiori-specific code.
 */

/* Compile this file as plain C99 (not C++). */

/*
 * Force every JS_EXTERN / JS_LIBC_EXTERN declaration in quickjs.h to become
 * __declspec(dllexport).  The compiler emits EXPDEF linker records for each
 * declaration; the linker resolves them against qjs.lib and places them in
 * quickshiori.dll's export table.
 */
#define BUILDING_QJS_SHARED

#include "quickjs.h"
#include "quickjs-libc.h"  /* also export the stdlib helpers (js_std_*, etc.) */

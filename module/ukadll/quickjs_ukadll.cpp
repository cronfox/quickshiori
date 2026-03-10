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

extern "C" {
#include "quickjs.h"
}

// ---------------------------------------------------------------------------
// Ukagaka DLL Interface Types (SAORI/SHIORI/PLUGIN)
// ---------------------------------------------------------------------------

typedef BOOL (__cdecl *UKADLL_LOADU)(HGLOBAL h, long len);
typedef BOOL (__cdecl *UKADLL_LOAD)(HGLOBAL h, long len);
typedef BOOL (__cdecl *UKADLL_UNLOAD)();
typedef HGLOBAL (__cdecl *UKADLL_REQUEST)(HGLOBAL h, long *len);

// ---------------------------------------------------------------------------
// Native Class Data
// ---------------------------------------------------------------------------

static JSClassID js_ukadll_class_id;

struct UkaDllData {
    HMODULE hModule = nullptr;
    UKADLL_LOAD pLoad = nullptr;
    UKADLL_LOADU pLoadU = nullptr;
    UKADLL_UNLOAD pUnload = nullptr;
    UKADLL_REQUEST pRequest = nullptr;
    std::string dllPath;
    bool isLoaded = false;
};

// ---------------------------------------------------------------------------
// Internal Helpers
// ---------------------------------------------------------------------------

static HGLOBAL string_to_hglobal(const std::string& s, long* out_len) {
    long len = static_cast<long>(s.size());
    if (out_len) *out_len = len;
    // Note: Some legacy DLLs might expect a null terminator even if len is provided
    HGLOBAL h = GlobalAlloc(GMEM_FIXED, len + 1);
    if (h) {
        memcpy(reinterpret_cast<char*>(h), s.data(), len);
        reinterpret_cast<char*>(h)[len] = '\0';
    }
    return h;
}

static std::string utf8_to_oemcp(const std::string& utf8) {
    if (utf8.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), wlen);

    int oemLen = WideCharToMultiByte(CP_OEMCP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string oem(oemLen, '\0');
    WideCharToMultiByte(CP_OEMCP, 0, wide.c_str(), -1, oem.data(), oemLen, nullptr, nullptr);
    // WideCharToMultiByte with -1 includes the null terminator in the count
    if (!oem.empty() && oem.back() == '\0') oem.pop_back();
    return oem;
}

static std::string hglobal_to_string(HGLOBAL h, long len) {
    if (!h) return "";
    std::string s(reinterpret_cast<char*>(h), len);
    // As per spec: "この返答の領域を開放するのが呼び出し側の責任である"
    GlobalFree(h);
    return s;
}

// ---------------------------------------------------------------------------
// JS Class Methods
// ---------------------------------------------------------------------------

static void js_ukadll_finalizer(JSRuntime *rt, JSValue val) {
    UkaDllData *data = static_cast<UkaDllData*>(JS_GetOpaque(val, js_ukadll_class_id));
    if (data) {
        if (data->isLoaded && data->pUnload) {
            data->pUnload();
        }
        if (data->hModule) {
            FreeLibrary(data->hModule);
        }
        delete data;
    }
}

static JSValue js_ukadll_ctor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "UkaDll requires a string path argument");
    
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    
    HMODULE h = LoadLibraryA(path);
    if (!h) {
        JSValue err = JS_ThrowReferenceError(ctx, "Failed to load DLL: %s (System Error: %lu)", path, GetLastError());
        JS_FreeCString(ctx, path);
        return err;
    }
    
    UkaDllData *data = new UkaDllData();
    data->hModule = h;
    data->dllPath = path;
    JS_FreeCString(ctx, path);
    
    data->pLoad = (UKADLL_LOAD)GetProcAddress(h, "load");
    data->pLoadU = (UKADLL_LOADU)GetProcAddress(h, "loadu");
    data->pUnload = (UKADLL_UNLOAD)GetProcAddress(h, "unload");
    data->pRequest = (UKADLL_REQUEST)GetProcAddress(h, "request");
    
    if (!data->pRequest) {
        FreeLibrary(h);
        delete data;
        return JS_ThrowTypeError(ctx, "DLL is not a valid Ukagaka module (exports 'request' missing)");
    }
    
    JSValue obj = JS_NewObjectClass(ctx, js_ukadll_class_id);
    if (JS_IsException(obj)) {
        FreeLibrary(h);
        delete data;
        return obj;
    }
    
    JS_SetOpaque(obj, data);
    return obj;
}

static JSValue js_ukadll_load(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    UkaDllData *data = static_cast<UkaDllData*>(JS_GetOpaque2(ctx, this_val, js_ukadll_class_id));
    if (!data) return JS_EXCEPTION;
    
    if (!data->hModule) return JS_ThrowReferenceError(ctx, "DLL module not loaded or already unloaded");

    if (!data->pLoad && !data->pLoadU) return JS_TRUE; // load is optional in some specs

    std::string dirPath;
    if (argc >= 1) {
        const char* s = JS_ToCString(ctx, argv[0]);
        if (s) {
            dirPath = s;
            JS_FreeCString(ctx, s);
        }
    } else {
        // Fallback: Use the parent directory of the DLL itself
        size_t lastSlash = data->dllPath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            dirPath = data->dllPath.substr(0, lastSlash + 1);
        }
    }

    // As per spec: "渡されるハンドルの解放は全て渡された側に委ねられており、渡した侧では解放を行わない"
    // The DLL is responsible for GlobalFree(h).
    if (data->pLoadU) {
        // loadu expects UTF-8 — pass the string as-is
        long len = 0;
        HGLOBAL h = string_to_hglobal(dirPath, &len);
        if (!h) return JS_ThrowOutOfMemory(ctx);
        if (data->pLoadU(h, len)) {
            data->isLoaded = true;
            return JS_TRUE;
        }
        return JS_FALSE;
    } else {
        // load expects CP_OEMCP — convert from UTF-8
        long len = 0;
        HGLOBAL h = string_to_hglobal(utf8_to_oemcp(dirPath), &len);
        if (!h) return JS_ThrowOutOfMemory(ctx);
        if (data->pLoad(h, len)) {
            data->isLoaded = true;
            return JS_TRUE;
        }
        return JS_FALSE;
    }
}

static JSValue js_ukadll_request(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    UkaDllData *data = static_cast<UkaDllData*>(JS_GetOpaque2(ctx, this_val, js_ukadll_class_id));
    if (!data) return JS_EXCEPTION;
    
    if (!data->hModule) return JS_ThrowReferenceError(ctx, "DLL module not loaded or already unloaded");
    
    if (argc < 1) return JS_ThrowTypeError(ctx, "UkaDll.request requires a string argument");
    
    size_t in_len_sz;
    const char* reqStr = JS_ToCStringLen(ctx, &in_len_sz, argv[0]);
    if (!reqStr) return JS_EXCEPTION;
    
    long len = static_cast<long>(in_len_sz);
    HGLOBAL hReq = string_to_hglobal(std::string(reqStr, in_len_sz), &len);
    JS_FreeCString(ctx, reqStr);
    
    if (!hReq) return JS_ThrowOutOfMemory(ctx);
    
    // DLL is responsible for freeing hReq.
    // It returns a new HGLOBAL and sets len.
    HGLOBAL hResp = data->pRequest(hReq, &len);
    if (!hResp) return JS_NULL;
    
    std::string respStr = hglobal_to_string(hResp, len); // We free hResp here
    return JS_NewStringLen(ctx, respStr.data(), respStr.size());
}

static JSValue js_ukadll_unload(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    UkaDllData *data = static_cast<UkaDllData*>(JS_GetOpaque2(ctx, this_val, js_ukadll_class_id));
    if (!data) return JS_EXCEPTION;

    BOOL result = TRUE;
    if (data->isLoaded && data->pUnload) {
        data->isLoaded = false;
        result = data->pUnload();
    }

    // Free the DLL immediately so a new instance can load a fresh copy
    if (data->hModule) {
        FreeLibrary(data->hModule);
        data->hModule = nullptr;
    }
    data->pLoad    = nullptr;
    data->pLoadU   = nullptr;
    data->pUnload  = nullptr;
    data->pRequest = nullptr;

    return result ? JS_TRUE : JS_FALSE;
}

// ---------------------------------------------------------------------------
// Module Definition
// ---------------------------------------------------------------------------

static const JSCFunctionListEntry js_ukadll_proto_funcs[] = {
    JS_CFUNC_DEF("load", 1, js_ukadll_load),
    JS_CFUNC_DEF("request", 1, js_ukadll_request),
    JS_CFUNC_DEF("unload", 0, js_ukadll_unload),
};

static int js_ukadll_module_init(JSContext *ctx, JSModuleDef *m) {
    JSRuntime *rt = JS_GetRuntime(ctx);
    
    JS_NewClassID(rt, &js_ukadll_class_id);
    JSClassDef class_def = { "UkaDll", js_ukadll_finalizer };
    JS_NewClass(rt, js_ukadll_class_id, &class_def);
    
    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, js_ukadll_proto_funcs, sizeof(js_ukadll_proto_funcs) / sizeof(js_ukadll_proto_funcs[0]));
    JS_SetClassProto(ctx, js_ukadll_class_id, proto);
    
    JSValue ctor = JS_NewCFunction2(ctx, js_ukadll_ctor, "UkaDll", 1, JS_CFUNC_constructor, 0);
    // Setting .prototype is standard for constructor functions
    JS_SetConstructor(ctx, ctor, proto);
    
    JS_SetModuleExport(ctx, m, "UkaDll", ctor);
    return 0;
}

extern "C" __declspec(dllexport) JSModuleDef* js_init_module(JSContext *ctx, const char *module_name) {
    JSModuleDef *m = JS_NewCModule(ctx, module_name, js_ukadll_module_init);
    if (!m) return nullptr;
    JS_AddModuleExport(ctx, m, "UkaDll");
    return m;
}

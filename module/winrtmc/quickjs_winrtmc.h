/*
 * Copyright (c) 2026 Cronfox
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QUICKJS_WINRTMC_H
#define QUICKJS_WINRTMC_H

#include "quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 将 "winrtmc" C 模块注册到 QuickJS 上下文（供静态链接时使用）。
 *
 * 与 DLL 导出的 js_init_module() 功能相同，但：
 *   - 不带 __declspec(dllexport)，适合内嵌到另一个 DLL 中
 *   - 模块名固定为 "winrtmc"
 *
 * 在 JS_NewContext() 之后、执行任何 JS 代码之前调用。
 */
JSModuleDef* js_init_module_winrtmc(JSContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* QUICKJS_WINRTMC_H */

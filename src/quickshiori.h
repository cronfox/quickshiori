/*
 * Copyright (c) 2026 Cronfox
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QUICKJS_CONNECTOR_H
#define QUICKJS_CONNECTOR_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// DLL 接口函数
BOOL __cdecl loadu(HGLOBAL h, long len);
BOOL __cdecl load(HGLOBAL h, long len);
HGLOBAL __cdecl request(HGLOBAL h, long* len);
BOOL __cdecl unload();

#ifdef __cplusplus
}
#endif

#endif // QUICKJS_CONNECTOR_H

/*
 * Copyright (c) 2026 Cronfox
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QUICKSHIORI_MODULE_H
#define QUICKSHIORI_MODULE_H

#include "quickjs.h"

// Register the built-in "quickshiori" C module with QuickJS.
// Must be called after JS_NewContext() and before any JS code is evaluated.
//
// The module exposes:
//   import { log, debug, info, warn, error, setLevel, setFileOutput,
//            process }
//     from "quickshiori";
//
//   process.version  : string  — quickshiori release version, e.g. "0.0.1"
//   process.versions : object  — { quickshiori: "0.0.1", "quickjs-ng": "0.12.1" }
JSModuleDef* js_init_module_quickshiori(JSContext* ctx, const char* module_name);

#endif // QUICKSHIORI_MODULE_H

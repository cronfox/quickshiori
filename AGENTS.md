# AGENTS.md - QuickShiori Development Guide

## Project Overview

QuickShiori is a C++ CMake project that embeds QuickJS-NG to provide JavaScript runtime support for the Ukagaka platform. It builds Windows DLLs implementing SHIORI/SAORI protocols.

## Build Commands

### Prerequisites
- Windows with MSVC (x86 architecture required)
- CMake 3.20+
- Ninja build system
- Git submodules (recursive clone required)

### Configure and Build
```bash
# Configure (x86 build required for Ukagaka compatibility)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build all targets
cmake --build build --parallel

# Build specific target
cmake --build build --target quickshiori
cmake --build build --target winrtmc_plugin
cmake --build build --target ukadll
```

### Build Types
- `Release` - Optimized build
- `Debug` - Debug symbols
- `RelWithDebInfo` - Release with debug info (recommended)

### Clean Build
```bash
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
```

## Project Structure

```
├── src/                    # Main SHIORI DLL source
│   ├── quickshiori.cpp     # DLL entry points (load/loadu/request/unload)
│   ├── quickshiori_module.* # QuickJS C module implementation
│   └── quickshiori_log.h   # Logging utilities
├── module/                 # Additional QuickJS modules
│   ├── ukadll/             # Ukagaka DLL wrapper module
│   └── winrtmc/            # WinRT MediaControl module
├── WinRTMCPlugin/          # Self-contained SHIORI DLL with bytecode
├── 3rd/                    # Third-party dependencies
│   ├── quickjs/            # QuickJS-NG engine
│   ├── qjs-ng-sqlite3/     # SQLite3 binding
│   └── sqlite3-cmake/      # SQLite3 CMake wrapper
├── types/                  # TypeScript type definitions
└── build/dist/             # Build outputs (qjs.dll, quickshiori.dll, etc.)
```

## Code Style Guidelines

### C++ Code Style

#### Naming Conventions
- **Functions**: `snake_case` (e.g., `cp_oemcp_to_utf8`, `collect_exception`)
- **Variables**: `snake_case` (e.g., `g_rt`, `g_ctx`, `g_dir`)
- **Global Variables**: prefix with `g_` (e.g., `g_log_level`, `g_initialized`)
- **Classes/Structs**: `PascalCase` (e.g., `LogLevel` enum class)
- **Constants**: `UPPER_CASE` or `kCamelCase`
- **Files**: `snake_case.cpp/.h`

#### Code Formatting
- **Indentation**: 4 spaces (no tabs)
- **Line endings**: LF (Unix-style)
- **Encoding**: UTF-8 with BOM for MSVC compatibility (`/utf-8` flag)
- **Brace style**: Same-line opening brace (K&R style)

```cpp
static std::string cp_oemcp_to_utf8(const char* src, int len) {
    int wlen = MultiByteToWideChar(CP_OEMCP, 0, src, len, nullptr, 0);
    // ...
}
```

#### Standards
- **C++ Standard**: C++17 for main project, C++20 for WinRTMCPlugin
- **Target Platform**: Windows x86 (32-bit)
- **Compiler**: MSVC with `/utf-8`, `/EHsc`, `/W3`

#### Headers
- All headers must include BSD 3-clause license
- Use include guards: `#ifndef FILE_NAME_H`
- Include order: Windows headers → C++ stdlib → 3rd party → local

```cpp
#ifndef QUICKSHIORI_LOG_H
#define QUICKSHIORI_LOG_H

#include <string>
// ...

#endif // QUICKSHIORI_LOG_H
```

### JavaScript/TypeScript Code Style

#### Naming Conventions
- **Variables**: `camelCase`
- **Functions**: `camelCase`
- **Classes**: `PascalCase`
- **Constants**: `UPPER_CASE`

#### Module System
- Use ES modules (`import`/`export`)
- TypeScript declarations in `.d.ts` files
- Support both CommonJS and ES module imports

```javascript
export class KashiwazakiParser {
    constructor(protocol) {
        this.protocol = protocol;
    }
}
```

#### Comments
- Use JSDoc for public APIs
- Support both English and Chinese comments

### Error Handling

#### C++
- Use return values with `std::pair<bool, std::string>` for error propagation
- Log errors via `log_error()` before returning
- Clean up resources on error paths (RAII preferred)

```cpp
static std::pair<bool, std::string> call_global(const char* name, const char* arg = nullptr) {
    // ...
    if (JS_IsException(ret)) {
        return {false, collect_exception()};
    }
    return {true, result};
}
```

#### JavaScript
- Throw `Error` objects with descriptive messages
- Validate inputs at function boundaries

### Logging

Use the built-in logging system:

```cpp
log_debug("Runtime created");
log_info("Initialization complete");
log_warn("Deprecated API used");
log_error("Failed to compile: " + err);
```

## CMake Guidelines

### Target Properties
- Set `CXX_STANDARD` per-target (17 or 20)
- Use `target_compile_definitions` for Windows macros:
  - `WIN32_LEAN_AND_MEAN`
  - `QUICKJS_NG_BUILD`
  - `NOMINMAX` (for WinRT)

### Output Configuration
- Runtime outputs go to `${CMAKE_BINARY_DIR}/dist`
- DLL prefix is empty (Windows convention)
- Suffix is `.dll`

## Testing

This project currently has no automated test suite. Testing is done via:
1. Integration testing with Ukagaka base ware (SSP)
2. Manual verification of DLL loading/unloading
3. JavaScript entry point validation

## CI/CD

GitHub Actions workflows in `.github/workflows/`:
- `ci-windows.yml` - Builds on Windows x86, uploads artifacts
- `release-windows.yml` - Release builds with versioned artifacts

## Important Notes

1. **Always use x86 architecture** - Ukagaka base ware is 32-bit
2. **UTF-8 encoding** - Required for Chinese comments and Japanese text handling
3. **Static linking** - WinRTMCPlugin is fully self-contained
4. **Version management** - Update `CMakeLists.txt` project version for releases
5. **Submodules** - Run `git submodule update --init --recursive` after clone

## License Headers

All C++ files must include the BSD 3-clause license header:

```cpp
/*
 * Copyright (c) 2026 Cronfox
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms...
 */
```

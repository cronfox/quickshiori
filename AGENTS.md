# AGENTS.md — QuickShiori Development Guide

## Project Overview

QuickShiori embeds QuickJS-NG to provide a JavaScript runtime for the [Ukagaka](https://ssp.shillest.net/ukadoc/manual/spec_dll.html) platform. It builds Windows x86 DLLs implementing SHIORI/SAORI/PLUGIN/HEADLINE protocols. Ghost authors write `index.js`; the runtime bridges JS ↔ base ware (SSP, Materia, CROW).

## Build Commands

### Prerequisites
- Windows with MSVC (x86 — Ukagaka base ware is 32-bit)
- CMake 3.20+, Ninja
- `git submodule update --init --recursive` after clone

### Configure & Build
```bash
# Configure (always x86)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build everything
cmake --build build --parallel

# Build a single target
cmake --build build --target quickshiori
cmake --build build --target ukadll
cmake --build build --target ukafmo
cmake --build build --target winrtmc
cmake --build build --target winrtmc_plugin
```

### Clean Rebuild
```bash
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
```

### Build Types
`RelWithDebInfo` (recommended) · `Release` · `Debug`

## Testing

No automated test framework. Tests are JS scripts run manually with the built `qjs.exe`:

```bash
# Run a module test (must run from repo root so module paths resolve)
build/dist/qjs.exe module/ukafmo/test_ukafmo.js

# Run the SQLite test
build/dist/qjs.exe 3rd/qjs-ng-sqlite3/test.js
```

Integration testing is done by loading DLLs in SSP and verifying SHIORI protocol responses.

## Project Structure

```
src/                    Main SHIORI runtime DLL (C++)
  quickshiori.cpp       DLL entry points: load/loadu/request/unload
  quickshiori_module.*  Built-in "quickshiori" QuickJS C module
  quickshiori_log.h     Logging subsystem
module/                 Native QuickJS modules (DLLs)
  ukadll/               Ukagaka DLL wrapper (load foreign SHIORI/SAORI)
  ukafmo/               FMO (shared memory) reader
  winrtmc/              WinRT MediaSession control
WinRTMCPlugin/          Self-contained SHIORI DLL with bytecode
3rd/                    Third-party: quickjs-ng, qjs-ng-sqlite3, sqlite3
types/                  TypeScript declarations for external use
build/dist/             Output: qjs.exe, qjs.dll, *.dll, *.pdb
```

## Code Style — C++

### Naming
| Element | Style | Example |
|---|---|---|
| Functions | `snake_case` | `cp_oemcp_to_utf8`, `collect_exception` |
| Local variables | `snake_case` | `wlen`, `base_path` |
| Global variables | `g_` prefix + `snake_case` | `g_rt`, `g_ctx`, `g_initialized` |
| Structs / enum class | `PascalCase` | `FmoEntry`, `LogLevel` |
| Constants | `UPPER_CASE` or `kCamelCase` | `CP_OEMCP` |

### Formatting
- 4 spaces, no tabs
- K&R brace style (opening brace on same line)
- LF line endings, UTF-8 with BOM via MSVC `/utf-8` flag

### Headers & Includes
- Include guard: `#ifndef FILE_NAME_H` / `#define FILE_NAME_H`
- Order: `<windows.h>` → C++ stdlib → 3rd party → local `""`
- Every `.cpp`/`.h` must start with the BSD 3-clause license header (see existing files for the exact text)

### Standards
- C++17 for main project, C++20 for WinRTMCPlugin
- MSVC flags: `/utf-8 /EHsc /W3`
- `WIN32_LEAN_AND_MEAN`, `QUICKJS_NG_BUILD` compile definitions

### Error Handling
- Return `std::pair<bool, std::string>` for fallible operations
- Log via `log_error()` before returning; never silently swallow errors
- RAII / manual cleanup on every error path (QuickJS values must be freed)

## Code Style — JavaScript / TypeScript

### Naming
| Element | Style |
|---|---|
| Variables, functions | `camelCase` |
| Classes | `PascalCase` |
| Constants | `UPPER_CASE` |

### Modules
- ES module syntax (`import`/`export`)
- Module DLLs are imported by their DLL name: `import { UkaDll } from "ukadll.dll"`
- Built-in modules: `import { info, warn } from "quickshiori"`

### TypeScript Declarations (`.d.ts`)
- One `.d.ts` per module in its directory (e.g. `module/ukafmo/ukafmo.d.ts`)
- Declare module with both plain name and `.dll` suffix:
  ```ts
  declare module 'ukafmo' { /* ... */ }
  declare module 'ukafmo.dll' { export * from 'ukafmo'; }
  ```
- Use JSDoc with `@param`, `@returns`, `@example` for all public APIs

### Global Hooks
Every ghost's `index.js` must define three globals:
```js
globalThis.__shiori_load = function (dir) { /* init */ };
globalThis.__shiori_request = function (rawRequest) { /* → response string */ };
globalThis.__shiori_unload = function () { /* cleanup */ };
```

## CMake Guidelines

- `CXX_STANDARD` set per target (17 or 20)
- Output to `${CMAKE_BINARY_DIR}/dist`, prefix `""`, suffix `.dll`
- Module DLLs link only `qjs` (not `qjs-libc`); main `quickshiori` links both
- All MSVC targets need `target_compile_options(... PRIVATE /utf-8)`

## CI/CD

GitHub Actions in `.github/workflows/`:
- `ci-windows.yml` — build on push/PR, upload artifact zip
- `release-windows.yml` — versioned release builds

Both use `ilammy/msvc-dev-cmd@v1` with `arch: x86`.

## Key Constraints

1. **Always x86** — base ware loads 32-bit DLLs only
2. **UTF-8 everywhere** — Chinese/Japanese text; MSVC `/utf-8` required
3. **No Node.js** — QuickJS is the runtime; no npm, no Node APIs
4. **Synchronous SHIORI** — `__shiori_request` must return a string, not a Promise
5. **Submodules required** — `3rd/` contains vendored dependencies

cmake_minimum_required(VERSION 3.20)
project(QuickShiori VERSION 0.0.1 LANGUAGES CXX) 

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")

configure_file(src/version.h.in src/version.h)
# Project License: BSD 3-Clause
# This project statically links QuickJS-NG which is licensed under the MIT License.
# See LICENSE file for complete license information.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# # ── 获取 QuickJS-ng（官方 CMake 支持的 QuickJS 分支）──────────────────────────
# include(FetchContent)
# FetchContent_Declare(
#     quickjs
#     GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
#     GIT_TAG        v0.12.1
#     GIT_SHALLOW    TRUE
# )
# # 仅构建静态库，不构建可执行文件
# # set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
# # set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
# FetchContent_MakeAvailable(quickjs)

add_subdirectory(./quickjs)

add_subdirectory(src)
add_subdirectory(module)
add_subdirectory(WinRTMCPlugin)


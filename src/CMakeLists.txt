# ── 构建 SHIORI DLL ──────────────────────────────────────────────────────────
add_library(quickshiori SHARED
    quickshiori.cpp
    quickshiori_module.cpp
)

target_include_directories(quickshiori PRIVATE
    ${quickjs_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/src   # generated version.h
)

target_link_libraries(quickshiori PUBLIC
    qjs-libc   # std / os 等内置模块实现（必须在 qjs 前）
    qjs        # quickjs-ng 核心静态库
)

# Windows DLL 需要导出符号
target_compile_definitions(quickshiori PRIVATE
    WIN32_LEAN_AND_MEAN
)
# MSVC 需要 /utf-8 防止中文注释报 C4819
if(MSVC)
    target_compile_options(quickshiori PRIVATE /utf-8)
endif()

# 输出文件名（伺か BaseWare 按固定文件名加载 SHIORI）
set_target_properties(quickshiori PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "quickshiori"
    PREFIX ""
    SUFFIX ".dll"
)
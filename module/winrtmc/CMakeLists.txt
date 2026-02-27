
# ── 构建 WinRT 媒体控制 QuickJS 模块 DLL ────────────────────────────────────
add_library(winrtmc SHARED
    quickjs_winrtmc.cpp
)


# C++/WinRT 头文件要求 C++20（协程为标准特性，无需 /await 标志）
set_target_properties(winrtmc PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "winrtmc"
    PREFIX ""
    SUFFIX ".dll"
)

target_include_directories(winrtmc PRIVATE
    ${quickjs_SOURCE_DIR}
)


target_link_libraries(winrtmc PRIVATE
    qjs               # quickjs-ng 核心（提供 JSContext/JSRuntime 等类型）
    runtimeobject.lib # WinRT 基础运行时（CoInitializeEx、RoActivateInstance 等）
)

if(MSVC)
    target_compile_options(winrtmc PRIVATE
        /utf-8            # 统一字符集，避免中文注释警告
        /EHsc             # 启用标准 C++ 异常
        /W3
    )
    target_compile_definitions(winrtmc PRIVATE
        WIN32_LEAN_AND_MEAN
        WINRT_LEAN_AND_MEAN   # 减少 C++/WinRT 头文件编译开销
        NOMINMAX
    )
endif()
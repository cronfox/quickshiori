add_library(ukafmo SHARED
    quickjs_ukafmo.cpp
)

set_target_properties(ukafmo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "ukafmo"
    PREFIX ""
    SUFFIX ".dll"
)

target_include_directories(ukafmo PRIVATE
    ${quickjs_SOURCE_DIR}
)

target_link_libraries(ukafmo PRIVATE
    qjs               # quickjs-ng 核心（提供 JSContext/JSRuntime 等类型）
)

if(MSVC)
    target_compile_options(ukafmo PRIVATE /utf-8)
endif()

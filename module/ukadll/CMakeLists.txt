add_library(ukadll SHARED
    quickjs_ukadll.cpp
)

set_target_properties(ukadll PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "ukadll"
    PREFIX ""
    SUFFIX ".dll"
)

target_include_directories(ukadll PRIVATE
    ${quickjs_SOURCE_DIR}
)

target_link_libraries(ukadll PRIVATE
    qjs               # quickjs-ng 核心（提供 JSContext/JSRuntime 等类型）
)

if(MSVC)
    target_compile_options(ukadll PRIVATE /utf-8)
endif()

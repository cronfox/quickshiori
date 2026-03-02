add_subdirectory(quickjs)
add_subdirectory(sqlite3-amalgamation)

# QuickJS SQLite3 binding
add_library(quickjs_sqlite3 SHARED qjs-sqlite3/sqlite3.c)
target_compile_definitions(quickjs_sqlite3 PUBLIC JS_SHARED_LIBRARY)
target_include_directories(quickjs_sqlite3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}                        # <quickjs/quickjs.h>
)
if(TARGET SQLite::SQLite3)
    set(SQLITE3_TARGET SQLite::SQLite3)
else()
    set(SQLITE3_TARGET SQLite3)
endif()
target_link_libraries(quickjs_sqlite3
    ${SQLITE3_TARGET}
    qjs       # quickjs-ng 核心
)

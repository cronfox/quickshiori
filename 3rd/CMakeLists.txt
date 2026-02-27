add_subdirectory(quickjs)
add_subdirectory(sqlite3-amalgamation)

# QuickJS SQLite3 binding
add_library(quickjs_sqlite3 SHARED qjs-sqlite3/sqlite3.c)
target_include_directories(quickjs_sqlite3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}                        # <quickjs/quickjs.h>
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3-amalgamation   # <sqlite3.h>
)
target_link_libraries(quickjs_sqlite3
    SQLite3   # sqlite3-amalgamation 导出的静态目标
    qjs       # quickjs-ng 核心
)

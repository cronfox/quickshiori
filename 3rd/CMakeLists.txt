# quickjs defines BUILD_SHARED_LIBS via option(... OFF). Because it has its own
# cmake_minimum_required(3.10), policy handling can ignore parent normal vars.
# Force the cache entry so qjs is really built as SHARED (qjs.dll on Windows).
get_property(_quickshiori_build_shared_libs_had_cache CACHE BUILD_SHARED_LIBS PROPERTY TYPE SET)
if(_quickshiori_build_shared_libs_had_cache)
    set(_quickshiori_build_shared_libs_cache_backup "${BUILD_SHARED_LIBS}")
endif()

set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries" FORCE)
add_subdirectory(quickjs)

if(_quickshiori_build_shared_libs_had_cache)
    set(BUILD_SHARED_LIBS "${_quickshiori_build_shared_libs_cache_backup}" CACHE BOOL "Build shared libraries" FORCE)
else()
    unset(BUILD_SHARED_LIBS CACHE)
endif()

add_subdirectory(qjs-ng-sqlite3)
add_subdirectory(sqlite3-cmake)

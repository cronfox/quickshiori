# quickjs honors BUILD_SHARED_LIBS, so force it ON only for this subdir.
set(_quickshiori_build_shared_libs_was_defined FALSE)
if(DEFINED BUILD_SHARED_LIBS)
	set(_quickshiori_build_shared_libs_was_defined TRUE)
	set(_quickshiori_build_shared_libs_backup "${BUILD_SHARED_LIBS}")
endif()

set(BUILD_SHARED_LIBS ON)
add_subdirectory(quickjs)

if(_quickshiori_build_shared_libs_was_defined)
	set(BUILD_SHARED_LIBS "${_quickshiori_build_shared_libs_backup}")
else()
	unset(BUILD_SHARED_LIBS)
endif()

add_subdirectory(qjs-ng-sqlite3)
add_subdirectory(sqlite3-cmake)

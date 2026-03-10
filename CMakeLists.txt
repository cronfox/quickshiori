cmake_minimum_required(VERSION 3.20)
project(QuickShiori VERSION 0.1.0 LANGUAGES C CXX) 

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist")

set(QUICKSHIORI_COMMIT_ID "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		OUTPUT_VARIABLE QUICKSHIORI_COMMIT_ID
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
	if(NOT QUICKSHIORI_COMMIT_ID)
		set(QUICKSHIORI_COMMIT_ID "unknown")
	endif()
endif()

configure_file(src/version.h.in src/version.h)
# Project License: BSD 3-Clause
# This project statically links QuickJS-NG which is licensed under the MIT License.
# See LICENSE file for complete license information.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(3rd)

add_subdirectory(src)
add_subdirectory(module)
add_subdirectory(WinRTMCPlugin)


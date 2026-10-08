cmake_minimum_required(VERSION 3.27)
project(cathode CXX C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)

find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  message("ccache found: ${CCACHE_PROGRAM}")
else ()
  message("ccache not found, building without it")
endif ()

message("System: ${CMAKE_SYSTEM_NAME}. Processor: ${CMAKE_SYSTEM_PROCESSOR}. Version: ${CMAKE_SYSTEM_VERSION}.")
message("Compiler: ${CMAKE_CXX_COMPILER_ID}. Version: ${CMAKE_CXX_COMPILER_VERSION}")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND ${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS 14.1)
  message(FATAL_ERROR "Compiling cathode requires a GNU compiler of at least version 14.1")
endif ()
set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 26)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-all-candidates")

if (APPLE)
  set(IS_APPLE TRUE)
  set(OS macos)
  set(ICONV -liconv)
  set(CMAKE_INSTALL_RPATH "@loader_path/../lib")
elseif (WIN32)
  set(IS_WINDOWS TRUE)
  set(OS windows)
  set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
else ()
  set(IS_LINUX TRUE)
  set(OS linux)
  set(ICONV)
  set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
endif ()

if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "arm64")
  set(IS_ARM64 TRUE)
endif ()

if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "x86_64")
  set(IS_X86_64 TRUE)
endif ()

set(CMAKE_INSTALL_PREFIX ${PROJECT_BINARY_DIR} CACHE PATH "..." FORCE)

set(cathode_NAME cathode)
set(cathode_VERSION_MAJOR 0)
set(cathode_VERSION_MINOR 0)
set(cathode_VERSION_PATCH 222)
set(cathode_VERSION "${cathode_VERSION_MAJOR}.${cathode_VERSION_MINOR}.${cathode_VERSION_PATCH}")
set(cathode_APPDIR ${CMAKE_INSTALL_PREFIX})
set(cathode_DATADIR ${CMAKE_INSTALL_PREFIX}/share)

#add_compile_options("-fsanitize=address")
#add_link_options("-fsanitize=address")

include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)

configure_file(
        "config.h.in"
        "config.h"
)

include_directories("${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}")

add_subdirectory(src/Util)
add_subdirectory(src/Lang)
add_subdirectory(rt)
add_subdirectory(src/App)

include_directories(. src)

install(DIRECTORY share DESTINATION .)

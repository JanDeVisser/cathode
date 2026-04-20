cmake_minimum_required(VERSION 3.27)
project(lia CXX C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
    message("ccache found: ${CCACHE_PROGRAM}")
else()
    message("ccache not found, building without it")
endif()

message("System: ${CMAKE_SYSTEM_NAME}. Processor: ${CMAKE_SYSTEM_PROCESSOR}. Version: ${CMAKE_SYSTEM_VERSION}.")
message("Compiler: ${CMAKE_CXX_COMPILER_ID}. Version: ${CMAKE_CXX_COMPILER_VERSION}")

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" AND ${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS 14.1)
    message(FATAL_ERROR "Compiling lia requires a GNU compiler of at least version 14.1")
endif()
set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 26)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-all-candidates")

if (APPLE)
    set(IS_APPLE TRUE)
    set(OS macos)
    set(ICONV -liconv)
elseif (WIN32)
    set(IS_WINDOWS TRUE)
    set(OS windows)
else ()
    set(IS_LINUX TRUE)
    set(OS linux)
    set(ICONV)
endif ()

if( "${CMAKE_SYSTEM_PROCESSOR}"  STREQUAL "arm64")
  set(IS_ARM64 TRUE)
endif()

if( "${CMAKE_SYSTEM_PROCESSOR}"  STREQUAL "x86_64")
  set(IS_X86_64 TRUE)
endif()

set(CMAKE_INSTALL_PREFIX ${PROJECT_BINARY_DIR} CACHE PATH "..." FORCE)

set(lia_NAME lia)
set(lia_VERSION_MAJOR 0)
set(lia_VERSION_MINOR 0)
set(lia_VERSION_PATCH 222)
set(lia_VERSION "${lia_VERSION_MAJOR}.${lia_VERSION_MINOR}.${lia_VERSION_PATCH}")
set(lia_APPDIR ${CMAKE_INSTALL_PREFIX})
set(lia_DATADIR ${CMAKE_INSTALL_PREFIX}/share)

#add_compile_options("-fsanitize=address")
#add_link_options("-fsanitize=address")

include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)

configure_file(
        "config.h.in"
        "config.h"
)

include_directories("${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}")

add_library(
        Util
        STATIC
        src/Util/Align.h
        src/Util/Arena.cpp
        src/Util/Checked.h
        src/Util/Defer.h
        src/Util/Error.cpp
        #       src/Util/Integer.h
        src/Util/IO.cpp
        src/Util/JSON.cpp
        src/Util/Lexer.h
        src/Util/Logging.cpp
        src/Util/Options.cpp
        src/Util/Pipe.cpp
        src/Util/Process.cpp
        src/Util/Resolve.cpp
        src/Util/StringScanner.h
        src/Util/StringUtil.cpp
        src/Util/Token.cpp
        src/Util/TokenLocation.h
        src/Util/Utf8.cpp
)

target_link_libraries(
        Util
        ${ICONV}
        dl
)

add_library(
    LiaLang
    STATIC
    src/Lang/Coerce.cpp
    src/Lang/Config.cpp
    src/Lang/Dump.cpp
    src/Lang/Fold.cpp
    src/Lang/Keyword.cpp
    src/Lang/Namespace.cpp
    src/Lang/Operator.cpp
    src/Lang/Resolve.cpp
    src/Lang/Parser.cpp
    src/Lang/Stamp.cpp
    src/Lang/SyntaxNode.cpp
    src/Lang/Type.cpp
    src/Lang/AST/Call.cpp
    src/Lang/AST/Constant.cpp
    src/Lang/AST/ControlFlow.cpp
    src/Lang/AST/Defer.cpp
    src/Lang/AST/Expression.cpp
    src/Lang/AST/Function.cpp
    src/Lang/AST/Identifier.cpp
    src/Lang/AST/Module.cpp
    src/Lang/AST/PreProcess.cpp
    src/Lang/AST/Switch.cpp
    src/Lang/AST/Type.cpp
    src/Lang/QBE/Context.cpp
    src/Lang/QBE/Execute.cpp
    src/Lang/QBE/Expr.cpp
    src/Lang/QBE/File.cpp
    src/Lang/QBE/Function.cpp
    src/Lang/QBE/Instruction.cpp
    src/Lang/QBE/Native.cpp
    src/Lang/QBE/Operator.cpp
    src/Lang/QBE/QBE.cpp
    src/Lang/QBE/QBEInternal.cpp
    src/Lang/QBE/Type.cpp
    src/Lang/QBE/Value.cpp
)

target_link_libraries(
    LiaLang
        PRIVATE
        liart
        Util
        trampoline
        m
)

add_subdirectory(rt)

include_directories(. src)

add_executable(
        lia
        src/Lang/Main.cpp
)

target_link_libraries(
        lia
        PRIVATE
        LiaLang
)

#add_compile_options("-fno-inline-functions")

install(TARGETS Util LiaLang lia
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        BUNDLE DESTINATION bundle)

install(DIRECTORY share DESTINATION .)

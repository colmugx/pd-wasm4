cmake_minimum_required(VERSION 3.19)
set(CMAKE_C_STANDARD 11)

set(ENVSDK $ENV{PLAYDATE_SDK_PATH})

if (NOT ${ENVSDK} STREQUAL "")
    file(TO_CMAKE_PATH ${ENVSDK} SDK)
else()
    execute_process(
        COMMAND bash -c "egrep '^\\s*SDKRoot' $HOME/.Playdate/config"
        COMMAND head -n 1
        COMMAND cut -c9-
        OUTPUT_VARIABLE SDK
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if (NOT EXISTS ${SDK})
    message(FATAL_ERROR "SDK Path not found; set PLAYDATE_SDK_PATH or ~/.Playdate/config")
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release")
set(CMAKE_XCODE_GENERATE_SCHEME TRUE)

set(PLAYDATE_GAME_NAME playdate_wamr)
set(PLAYDATE_GAME_DEVICE playdate_wamr_DEVICE)

project(${PLAYDATE_GAME_NAME} C ASM)

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wasm-micro-runtime)
if (NOT EXISTS ${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
    message(FATAL_ERROR "WAMR not found at ${WAMR_ROOT_DIR}. Clone wasm-micro-runtime first.")
endif()

string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_LIBC_UVWASI 0)
set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_REF_TYPES 0)

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

if (TOOLCHAIN STREQUAL "armgcc")
    add_executable(
        ${PLAYDATE_GAME_DEVICE}
        src/main.c
        src/wamr_bridge.c
        ${WAMR_RUNTIME_LIB_SOURCE}
    )
else()
    add_library(
        ${PLAYDATE_GAME_NAME}
        SHARED
        src/main.c
        src/wamr_bridge.c
        ${WAMR_RUNTIME_LIB_SOURCE}
    )
endif()

include(${SDK}/C_API/buildsupport/playdate_game.cmake)

if (TOOLCHAIN STREQUAL "armgcc")
    set(TARGET_NAME ${PLAYDATE_GAME_DEVICE})
else()
    set(TARGET_NAME ${PLAYDATE_GAME_NAME})
endif()

target_include_directories(${TARGET_NAME} PUBLIC
    ${SDK}/C_API
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${WAMR_ROOT_DIR}/core/iwasm/include
)

if (NOT MSVC)
    target_link_libraries(${TARGET_NAME} m)
endif()

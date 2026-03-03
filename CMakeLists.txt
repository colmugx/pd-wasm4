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
set(PRODUCT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE STRING "Built PDX directory")

project(${PLAYDATE_GAME_NAME} C ASM)

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wasm-micro-runtime)
if (NOT EXISTS ${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
    message(FATAL_ERROR "WAMR not found at ${WAMR_ROOT_DIR}. Clone wasm-micro-runtime first.")
endif()

string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)
if (TOOLCHAIN STREQUAL "armgcc")
    set(WAMR_BUILD_TARGET "THUMB_VFP")
    set(SHARED_PLATFORM_CONFIG
        ${CMAKE_CURRENT_SOURCE_DIR}/src/wamr_playdate_platform.cmake)
endif()
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
        third_party/wasm4-core/apu.c
        third_party/wasm4-core/framebuffer.c
        third_party/wasm4-core/util.c
        ${WAMR_RUNTIME_LIB_SOURCE}
    )
else()
    add_library(
        ${PLAYDATE_GAME_NAME}
        SHARED
        src/main.c
        src/wamr_bridge.c
        third_party/wasm4-core/apu.c
        third_party/wasm4-core/framebuffer.c
        third_party/wasm4-core/util.c
        ${WAMR_RUNTIME_LIB_SOURCE}
    )
endif()

include(${SDK}/C_API/buildsupport/playdate.cmake)

if (TOOLCHAIN STREQUAL "armgcc")
    set(TARGET_NAME ${PLAYDATE_GAME_DEVICE})
    set(PDX_PACKAGE_NAME ${PLAYDATE_GAME_DEVICE})
    set_property(TARGET ${TARGET_NAME} PROPERTY OUTPUT_NAME "${PLAYDATE_GAME_DEVICE}.elf")
else()
    set(TARGET_NAME ${PLAYDATE_GAME_NAME})
    set(PDX_PACKAGE_NAME ${PLAYDATE_GAME_NAME})
endif()

target_include_directories(${TARGET_NAME} PUBLIC
    ${SDK}/C_API
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wasm4-core
    ${WAMR_ROOT_DIR}/core/iwasm/include
)

if (NOT MSVC)
    target_link_libraries(${TARGET_NAME} m)
endif()

set(STAGING_DIR ${CMAKE_CURRENT_BINARY_DIR}/staging)
set(STAGING_SOURCE_DIR ${STAGING_DIR}/Source)

if (TOOLCHAIN STREQUAL "armgcc")
    set(PDEX_STAGE_FILE ${STAGING_SOURCE_DIR}/pdex.elf)
else()
    if (MSVC OR MINGW)
        set(PDEX_EXT dll)
    elseif(APPLE)
        set(PDEX_EXT dylib)
    elseif(UNIX)
        set(PDEX_EXT so)
    else()
        message(FATAL_ERROR "Platform not supported!")
    endif()
    set(PDEX_STAGE_FILE ${STAGING_SOURCE_DIR}/pdex.${PDEX_EXT})
endif()

add_custom_command(TARGET ${TARGET_NAME} PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PRODUCT_DIR}
)

if (TOOLCHAIN STREQUAL "armgcc")
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${STAGING_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${STAGING_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Source ${STAGING_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E rm -f
            ${STAGING_SOURCE_DIR}/pdex.bin
            ${STAGING_SOURCE_DIR}/pdex.elf
            ${STAGING_SOURCE_DIR}/pdex.dylib
            ${STAGING_SOURCE_DIR}/pdex.dll
            ${STAGING_SOURCE_DIR}/pdex.so
        COMMAND ${CMAKE_STRIP} --strip-unneeded -R .comment -g $<TARGET_FILE:${TARGET_NAME}> -o ${PDEX_STAGE_FILE}
        COMMAND ${PDC} ${STAGING_SOURCE_DIR} ${PRODUCT_DIR}/${PDX_PACKAGE_NAME}.pdx
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM
    )
else()
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${STAGING_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${STAGING_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Source ${STAGING_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E rm -f
            ${STAGING_SOURCE_DIR}/pdex.bin
            ${STAGING_SOURCE_DIR}/pdex.elf
            ${STAGING_SOURCE_DIR}/pdex.dylib
            ${STAGING_SOURCE_DIR}/pdex.dll
            ${STAGING_SOURCE_DIR}/pdex.so
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${TARGET_NAME}> ${PDEX_STAGE_FILE}
        COMMAND ${PDC} ${STAGING_SOURCE_DIR} ${PRODUCT_DIR}/${PDX_PACKAGE_NAME}.pdx
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM
    )
endif()

set_property(
    TARGET ${TARGET_NAME} APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    ${STAGING_DIR}
)
set_property(
    TARGET ${TARGET_NAME} APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    ${PRODUCT_DIR}/${PDX_PACKAGE_NAME}.pdx
)

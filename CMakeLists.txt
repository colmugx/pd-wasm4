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

set(PLAYDATE_GAME_NAME playdate-wasm-4)
set(PLAYDATE_GAME_DEVICE playdate-wasm-4-device)
set(PRODUCT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE STRING "Built PDX directory")

project(${PLAYDATE_GAME_NAME} C ASM)

option(WAMR_PD_USE_POOL_ALLOC "Use WAMR pool allocator instead of system allocator" OFF)
set(WAMR_PD_POOL_SIZE_KB "256" CACHE STRING "Pool allocator size in KB (used when WAMR_PD_USE_POOL_ALLOC=ON)")
set_property(CACHE WAMR_PD_POOL_SIZE_KB PROPERTY STRINGS 192 256 320)
set(WAMR_PD_AUDIO_CHUNK_FRAMES "512" CACHE STRING "Audio chunk frames for w4_apuWriteSamples")
set(WAMR_PD_AUDIO_BACKEND "native" CACHE STRING "Audio backend (native|wasm4_compat)")
set_property(CACHE WAMR_PD_AUDIO_BACKEND PROPERTY STRINGS native wasm4_compat)
set(WAMR_PD_LOGIC_TICK_DIVIDER "1" CACHE STRING "Run wasm update every N frames")
set_property(CACHE WAMR_PD_LOGIC_TICK_DIVIDER PROPERTY STRINGS 1 2 3)
option(WAMR_PD_DISABLE_AUDIO_TICK "Disable audio tick and output for profiling" OFF)
set(WAMR_PD_COMPOSITE_MODE "0" CACHE STRING "Composite mode (0=normal,1=minimal)")
set_property(CACHE WAMR_PD_COMPOSITE_MODE PROPERTY STRINGS 0 1)
option(WAMR_PD_ENABLE_AOT "Enable WAMR AOT support with interpreter fallback" ON)
option(WAMR_PD_TRUSTED_CARTS_ONLY "Only run trusted cartridges (required for mini loader experiment)" OFF)
option(WAMR_PD_ENABLE_MINI_LOADER_EXPERIMENT "Enable WAMR mini loader experiment (trusted carts only)" OFF)

if (NOT WAMR_PD_POOL_SIZE_KB MATCHES "^[0-9]+$")
    message(FATAL_ERROR "WAMR_PD_POOL_SIZE_KB must be a positive integer")
endif()
if (NOT WAMR_PD_AUDIO_CHUNK_FRAMES MATCHES "^[0-9]+$")
    message(FATAL_ERROR "WAMR_PD_AUDIO_CHUNK_FRAMES must be a positive integer")
endif()
if (WAMR_PD_AUDIO_CHUNK_FRAMES LESS 64)
    message(FATAL_ERROR "WAMR_PD_AUDIO_CHUNK_FRAMES must be >= 64")
endif()
if (NOT WAMR_PD_LOGIC_TICK_DIVIDER MATCHES "^[0-9]+$")
    message(FATAL_ERROR "WAMR_PD_LOGIC_TICK_DIVIDER must be a positive integer")
endif()
if (WAMR_PD_LOGIC_TICK_DIVIDER LESS 1)
    message(FATAL_ERROR "WAMR_PD_LOGIC_TICK_DIVIDER must be >= 1")
endif()
if (NOT WAMR_PD_COMPOSITE_MODE MATCHES "^[0-9]+$")
    message(FATAL_ERROR "WAMR_PD_COMPOSITE_MODE must be an integer")
endif()
if (NOT WAMR_PD_COMPOSITE_MODE STREQUAL "0" AND NOT WAMR_PD_COMPOSITE_MODE STREQUAL "1")
    message(FATAL_ERROR "WAMR_PD_COMPOSITE_MODE must be 0 (normal) or 1 (minimal)")
endif()
if (NOT WAMR_PD_AUDIO_BACKEND STREQUAL "native" AND NOT WAMR_PD_AUDIO_BACKEND STREQUAL "wasm4_compat")
    message(FATAL_ERROR "WAMR_PD_AUDIO_BACKEND must be native or wasm4_compat")
endif()

math(EXPR WAMR_PD_POOL_SIZE_BYTES "${WAMR_PD_POOL_SIZE_KB} * 1024")

if (WAMR_PD_USE_POOL_ALLOC)
    set(WAMR_PD_USE_POOL_ALLOC_NUM 1)
else()
    set(WAMR_PD_USE_POOL_ALLOC_NUM 0)
endif()
if (WAMR_PD_DISABLE_AUDIO_TICK)
    set(WAMR_PD_DISABLE_AUDIO_TICK_NUM 1)
else()
    set(WAMR_PD_DISABLE_AUDIO_TICK_NUM 0)
endif()
if (WAMR_PD_ENABLE_AOT)
    set(WAMR_PD_ENABLE_AOT_NUM 1)
else()
    set(WAMR_PD_ENABLE_AOT_NUM 0)
endif()
if (WAMR_PD_AUDIO_BACKEND STREQUAL "native")
    set(WAMR_PD_AUDIO_BACKEND_NUM 1)
else()
    set(WAMR_PD_AUDIO_BACKEND_NUM 0)
endif()

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wasm-micro-runtime)
if (NOT EXISTS ${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
    message(FATAL_ERROR "WAMR not found at ${WAMR_ROOT_DIR}. Clone wasm-micro-runtime first.")
endif()

string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)
if (TOOLCHAIN STREQUAL "armgcc")
    set(WAMR_BUILD_TARGET "THUMBV7EM_VFP")
    set(SHARED_PLATFORM_CONFIG
        ${CMAKE_CURRENT_SOURCE_DIR}/src/wamr_playdate_platform.cmake)
endif()
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT ${WAMR_PD_ENABLE_AOT_NUM})
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)

set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_LIBC_UVWASI 0)

set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_REF_TYPES 0)
set(WAMR_BUILD_MEMORY64 0)
set(WAMR_BUILD_MULTI_MEMORY 0)
set(WAMR_BUILD_SHARED_MEMORY 0)
set(WAMR_BUILD_SHARED_HEAP 0)
set(WAMR_BUILD_BULK_MEMORY 1)
set(WAMR_BUILD_BULK_MEMORY_OPT 1)
set(WAMR_BUILD_SHRUNK_MEMORY 1)

set(WAMR_BUILD_THREAD_MGR 0)
set(WAMR_BUILD_LIB_PTHREAD 0)
set(WAMR_BUILD_LIB_PTHREAD_SEMAPHORE 0)
set(WAMR_BUILD_LIB_WASI_THREADS 0)

set(WAMR_BUILD_PERF_PROFILING 0)
set(WAMR_BUILD_MEMORY_PROFILING 0)
set(WAMR_BUILD_DUMP_CALL_STACK 0)
set(WAMR_BUILD_CUSTOM_NAME_SECTION 0)
set(WAMR_CONFIGURABLE_BOUNDS_CHECKS 0)
set(WAMR_BUILD_INSTRUCTION_METERING 0)
set(WAMR_DISABLE_APP_ENTRY 1)

if (WAMR_PD_ENABLE_MINI_LOADER_EXPERIMENT AND WAMR_PD_TRUSTED_CARTS_ONLY)
    set(WAMR_BUILD_MINI_LOADER 1)
else()
    set(WAMR_BUILD_MINI_LOADER 0)
    if (WAMR_PD_ENABLE_MINI_LOADER_EXPERIMENT AND NOT WAMR_PD_TRUSTED_CARTS_ONLY)
        message(WARNING "WAMR mini loader experiment requires WAMR_PD_TRUSTED_CARTS_ONLY=ON; forcing WAMR_BUILD_MINI_LOADER=0")
    endif()
endif()

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_library(
    game_backend
    STATIC
    src/backend/game_backend.c
    src/backend/cart_catalog.c
    src/backend/host_runtime.c
    src/backend/input_mapper.c
    src/backend/menu_controller.c
    src/backend/persistence.c
    src/backend/render_composer.c
    src/backend/runtime_session.c
    src/wamr_bridge.c
    src/app/app_lifecycle.c
    src/app/lua_bindings.c
    src/app/runtime_service.c
    src/host_audio.c
    third_party/wasm4-core/apu.c
    third_party/wasm4-core/framebuffer.c
    third_party/wasm4-core/util.c
    ${WAMR_RUNTIME_LIB_SOURCE}
)

target_include_directories(game_backend PUBLIC
    ${SDK}/C_API
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wasm4-core
    ${WAMR_ROOT_DIR}/core/iwasm/include
)
target_compile_definitions(game_backend PRIVATE
    WAMR_PD_USE_POOL_ALLOC=${WAMR_PD_USE_POOL_ALLOC_NUM}
    WAMR_PD_POOL_SIZE_BYTES=${WAMR_PD_POOL_SIZE_BYTES}
    WAMR_PD_AUDIO_CHUNK_FRAMES=${WAMR_PD_AUDIO_CHUNK_FRAMES}
    WAMR_PD_LOGIC_TICK_DIVIDER=${WAMR_PD_LOGIC_TICK_DIVIDER}
    WAMR_PD_DISABLE_AUDIO_TICK=${WAMR_PD_DISABLE_AUDIO_TICK_NUM}
    WAMR_PD_AUDIO_BACKEND=${WAMR_PD_AUDIO_BACKEND_NUM}
    WAMR_PD_COMPOSITE_MODE=${WAMR_PD_COMPOSITE_MODE}
    WAMR_PD_ENABLE_AOT=${WAMR_PD_ENABLE_AOT_NUM}
)

if (TOOLCHAIN STREQUAL "armgcc")
    add_executable(
        ${PLAYDATE_GAME_DEVICE}
        src/main.c
    )
else()
    add_library(
        ${PLAYDATE_GAME_NAME}
        SHARED
        src/main.c
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

target_include_directories(${TARGET_NAME} PRIVATE
    ${SDK}/C_API
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(${TARGET_NAME} PRIVATE game_backend)

if (NOT MSVC)
    target_link_libraries(${TARGET_NAME} PRIVATE m)
endif()

set(STAGING_DIR ${CMAKE_CURRENT_BINARY_DIR}/staging)
set(STAGING_SOURCE_DIR ${STAGING_DIR}/Source)
set(SIZE_REPORT_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/scripts/report_pdx_sizes.cmake)
file(GLOB_RECURSE PDX_SOURCE_ASSETS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*
)

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

set(PDX_PACKAGE_STAMP ${CMAKE_CURRENT_BINARY_DIR}/${PDX_PACKAGE_NAME}.pdx.stamp)

if (TOOLCHAIN STREQUAL "armgcc")
    add_custom_command(
        OUTPUT ${PDX_PACKAGE_STAMP}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PRODUCT_DIR}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${STAGING_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${STAGING_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Source ${STAGING_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${STAGING_SOURCE_DIR}/cart
        COMMAND ${CMAKE_COMMAND} -E rm -f
            ${STAGING_SOURCE_DIR}/pdex.bin
            ${STAGING_SOURCE_DIR}/pdex.elf
            ${STAGING_SOURCE_DIR}/pdex.dylib
            ${STAGING_SOURCE_DIR}/pdex.dll
            ${STAGING_SOURCE_DIR}/pdex.so
        COMMAND ${CMAKE_STRIP} --strip-unneeded -R .comment -g $<TARGET_FILE:${TARGET_NAME}> -o ${PDEX_STAGE_FILE}
        COMMAND ${PDC} ${STAGING_SOURCE_DIR} ${PRODUCT_DIR}/${PDX_PACKAGE_NAME}.pdx
        COMMAND ${CMAKE_COMMAND}
            -DPDEX_FILE=${PDEX_STAGE_FILE}
            -DPDX_FILE=${PRODUCT_DIR}/${PDX_PACKAGE_NAME}.pdx
            -DPDX_NAME=${PDX_PACKAGE_NAME}
            -P ${SIZE_REPORT_SCRIPT}
        COMMAND ${CMAKE_COMMAND} -E touch ${PDX_PACKAGE_STAMP}
        DEPENDS ${TARGET_NAME} ${PDX_SOURCE_ASSETS} ${SIZE_REPORT_SCRIPT}
        BYPRODUCTS ${PRODUCT_DIR}/${PDX_PACKAGE_NAME}.pdx
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT ${PDX_PACKAGE_STAMP}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PRODUCT_DIR}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${STAGING_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${STAGING_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/Source ${STAGING_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${STAGING_SOURCE_DIR}/cart
        COMMAND ${CMAKE_COMMAND} -E rm -f
            ${STAGING_SOURCE_DIR}/pdex.bin
            ${STAGING_SOURCE_DIR}/pdex.elf
            ${STAGING_SOURCE_DIR}/pdex.dylib
            ${STAGING_SOURCE_DIR}/pdex.dll
            ${STAGING_SOURCE_DIR}/pdex.so
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${TARGET_NAME}> ${PDEX_STAGE_FILE}
        COMMAND ${PDC} ${STAGING_SOURCE_DIR} ${PRODUCT_DIR}/${PDX_PACKAGE_NAME}.pdx
        COMMAND ${CMAKE_COMMAND}
            -DPDEX_FILE=${PDEX_STAGE_FILE}
            -DPDX_FILE=${PRODUCT_DIR}/${PDX_PACKAGE_NAME}.pdx
            -DPDX_NAME=${PDX_PACKAGE_NAME}
            -P ${SIZE_REPORT_SCRIPT}
        COMMAND ${CMAKE_COMMAND} -E touch ${PDX_PACKAGE_STAMP}
        DEPENDS ${TARGET_NAME} ${PDX_SOURCE_ASSETS} ${SIZE_REPORT_SCRIPT}
        BYPRODUCTS ${PRODUCT_DIR}/${PDX_PACKAGE_NAME}.pdx
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        VERBATIM
    )
endif()

add_custom_target(${PDX_PACKAGE_NAME}_package ALL
    DEPENDS ${PDX_PACKAGE_STAMP}
)

set_property(
    TARGET ${TARGET_NAME} APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    ${STAGING_DIR}
)
set_property(
    TARGET ${TARGET_NAME} APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    ${PRODUCT_DIR}/${PDX_PACKAGE_NAME}.pdx
)
set_property(
    TARGET ${TARGET_NAME} APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    ${PDX_PACKAGE_STAMP}
)

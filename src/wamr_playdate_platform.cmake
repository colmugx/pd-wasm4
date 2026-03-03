set(PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

add_definitions(-DBH_PLATFORM_PLAYDATE)

include_directories(${PLATFORM_SHARED_DIR})
include_directories(${WAMR_ROOT_DIR}/core/shared/platform/include)

include(${WAMR_ROOT_DIR}/core/shared/platform/common/math/platform_api_math.cmake)
include(${WAMR_ROOT_DIR}/core/shared/platform/common/memory/platform_api_memory.cmake)

set(PLATFORM_SHARED_SOURCE
    ${PLATFORM_SHARED_DIR}/wamr_playdate_platform.c
    ${PLATFORM_COMMON_MATH_SOURCE}
    ${PLATFORM_COMMON_MEMORY_SOURCE}
)

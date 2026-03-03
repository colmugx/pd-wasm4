#ifndef PLAYDATE_WAMR_W4_MEMORY_MAP_H
#define PLAYDATE_WAMR_W4_MEMORY_MAP_H

#include <stdint.h>

#ifndef W4_WIDTH
#define W4_WIDTH 160
#endif

#ifndef W4_HEIGHT
#define W4_HEIGHT 160
#endif

#ifndef W4_MEMORY_SIZE
#define W4_MEMORY_SIZE (64 * 1024)
#endif

#ifndef W4_FRAMEBUFFER_OFFSET
#define W4_FRAMEBUFFER_OFFSET 0x00a0
#endif

#ifndef W4_FRAMEBUFFER_SIZE
#define W4_FRAMEBUFFER_SIZE (W4_WIDTH * W4_HEIGHT / 4)
#endif

#ifndef W4_SYSTEM_PRESERVE_FRAMEBUFFER
#define W4_SYSTEM_PRESERVE_FRAMEBUFFER 1
#endif

#pragma pack(push, 1)
typedef struct W4MemoryMap {
    uint8_t _padding[4];
    uint32_t palette[4];
    uint8_t drawColors[2];
    uint8_t gamepads[4];
    int16_t mouseX;
    int16_t mouseY;
    uint8_t mouseButtons;
    uint8_t systemFlags;
    uint8_t _reserved[128];
    uint8_t framebuffer[W4_FRAMEBUFFER_SIZE];
} W4MemoryMap;
#pragma pack(pop)

#endif

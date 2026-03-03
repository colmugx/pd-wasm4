#include "backend/render_composer.h"

#include <stdint.h>
#include <string.h>

#include "util.h"

#define W4_DITHER_NONE 0
#define W4_RENDER_MODE_FAST_160 0
#define W4_COMPOSITE_MODE_MINIMAL 1

#ifndef WAMR_PD_COMPOSITE_MODE
#define WAMR_PD_COMPOSITE_MODE 0
#endif

#define W4_MAX_VIEWPORT_SIZE LCD_ROWS

static const uint8_t g_bayer2[2][2] = {
    { 0, 2 },
    { 3, 1 },
};

static int g_scale_map_size;
static uint16_t g_scale_x0[W4_MAX_VIEWPORT_SIZE];
static uint16_t g_scale_x1[W4_MAX_VIEWPORT_SIZE];
static uint16_t g_scale_xw[W4_MAX_VIEWPORT_SIZE];
static uint16_t g_scale_y0[W4_MAX_VIEWPORT_SIZE];
static uint16_t g_scale_y1[W4_MAX_VIEWPORT_SIZE];
static uint16_t g_scale_yw[W4_MAX_VIEWPORT_SIZE];
static uint8_t g_src_luma[W4_WIDTH * W4_HEIGHT];

static uint8_t
palette_luma(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xff;
    uint32_t g = (rgb >> 8) & 0xff;
    uint32_t b = rgb & 0xff;

    return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

static void
get_viewport_rect(int render_mode, int *x, int *y, int *size)
{
    int viewport_size = (render_mode == W4_RENDER_MODE_FAST_160) ? W4_WIDTH : LCD_ROWS;

    if (size) {
        *size = viewport_size;
    }
    if (x) {
        *x = (LCD_COLUMNS - viewport_size) / 2;
    }
    if (y) {
        *y = (LCD_ROWS - viewport_size) / 2;
    }
}

static void
init_scale_map(int viewport_size)
{
    int i;

    if (viewport_size <= 1 || viewport_size > W4_MAX_VIEWPORT_SIZE) {
        return;
    }

    if (g_scale_map_size == viewport_size) {
        return;
    }

    for (i = 0; i < viewport_size; i++) {
        uint32_t x_fp =
            ((uint32_t)i * (uint32_t)(W4_WIDTH - 1) << 16) / (viewport_size - 1);
        uint32_t y_fp =
            ((uint32_t)i * (uint32_t)(W4_HEIGHT - 1) << 16) / (viewport_size - 1);
        uint16_t x0 = (uint16_t)(x_fp >> 16);
        uint16_t y0 = (uint16_t)(y_fp >> 16);

        g_scale_x0[i] = x0;
        g_scale_y0[i] = y0;
        g_scale_x1[i] = (x0 + 1 < W4_WIDTH) ? (uint16_t)(x0 + 1) : x0;
        g_scale_y1[i] = (y0 + 1 < W4_HEIGHT) ? (uint16_t)(y0 + 1) : y0;
        g_scale_xw[i] = (uint16_t)(x_fp & 0xffffu);
        g_scale_yw[i] = (uint16_t)(y_fp & 0xffffu);
    }

    g_scale_map_size = viewport_size;
}

static void
build_threshold_tables(uint8_t binary_threshold, uint8_t luma_min,
                       uint32_t luma_range, uint8_t binary_black[256],
                       uint8_t ordered_black[4][256])
{
    int i;

    for (i = 0; i < 256; i++) {
        uint32_t normalized =
            ((uint32_t)(i - luma_min) * 255u + (luma_range / 2u)) / luma_range;
        int t00 = (int)g_bayer2[0][0] * 64 + 32;
        int t01 = (int)g_bayer2[0][1] * 64 + 32;
        int t10 = (int)g_bayer2[1][0] * 64 + 32;
        int t11 = (int)g_bayer2[1][1] * 64 + 32;

        binary_black[i] = (uint8_t)(i < binary_threshold);
        ordered_black[0][i] = (uint8_t)((int)normalized < t00);
        ordered_black[1][i] = (uint8_t)((int)normalized < t01);
        ordered_black[2][i] = (uint8_t)((int)normalized < t10);
        ordered_black[3][i] = (uint8_t)((int)normalized < t11);
    }
}

static void
build_fast160_nibble_tables(const uint8_t luma[4], uint8_t binary_threshold,
                            uint8_t luma_min, uint32_t luma_range,
                            uint8_t binary_nibble[256],
                            uint8_t ordered_nibble[2][256])
{
    int b;

    for (b = 0; b < 256; b++) {
        int c0 = b & 0x3;
        int c1 = (b >> 2) & 0x3;
        int c2 = (b >> 4) & 0x3;
        int c3 = (b >> 6) & 0x3;
        int y_parity;
        uint8_t bits = 0;

        bits |= (uint8_t)((luma[c0] < binary_threshold) ? 0x8 : 0x0);
        bits |= (uint8_t)((luma[c1] < binary_threshold) ? 0x4 : 0x0);
        bits |= (uint8_t)((luma[c2] < binary_threshold) ? 0x2 : 0x0);
        bits |= (uint8_t)((luma[c3] < binary_threshold) ? 0x1 : 0x0);
        binary_nibble[b] = bits;

        for (y_parity = 0; y_parity < 2; y_parity++) {
            uint32_t n0 =
                ((uint32_t)(luma[c0] - luma_min) * 255u + (luma_range / 2u))
                / luma_range;
            uint32_t n1 =
                ((uint32_t)(luma[c1] - luma_min) * 255u + (luma_range / 2u))
                / luma_range;
            uint32_t n2 =
                ((uint32_t)(luma[c2] - luma_min) * 255u + (luma_range / 2u))
                / luma_range;
            uint32_t n3 =
                ((uint32_t)(luma[c3] - luma_min) * 255u + (luma_range / 2u))
                / luma_range;
            int t_even = (int)g_bayer2[y_parity][0] * 64 + 32;
            int t_odd = (int)g_bayer2[y_parity][1] * 64 + 32;
            uint8_t nibble = 0;

            nibble |= (uint8_t)(((int)n0 < t_even) ? 0x8 : 0x0);
            nibble |= (uint8_t)(((int)n1 < t_odd) ? 0x4 : 0x0);
            nibble |= (uint8_t)(((int)n2 < t_even) ? 0x2 : 0x0);
            nibble |= (uint8_t)(((int)n3 < t_odd) ? 0x1 : 0x0);
            ordered_nibble[y_parity][b] = nibble;
        }
    }
}

void
render_composer_composite(PlaydateAPI *pd, W4MemoryMap *w4_mem, int render_mode,
                          int dither_mode, bool *framebuffer_clear_needed)
{
    uint8_t *frame;
    uint8_t luma[4];
    uint8_t sorted_luma[4];
    uint8_t binary_black[256];
    uint8_t ordered_black[4][256];
    int viewport_x;
    int viewport_y;
    int viewport_size;
    int x, y;
    uint32_t pixel_index;
    uint8_t binary_threshold;
    uint8_t luma_min;
    uint32_t luma_range;

    if (!pd || !pd->graphics || !w4_mem || !framebuffer_clear_needed) {
        return;
    }

    frame = pd->graphics->getFrame();
    if (!frame) {
        return;
    }
    get_viewport_rect(render_mode, &viewport_x, &viewport_y, &viewport_size);

#if WAMR_PD_COMPOSITE_MODE == W4_COMPOSITE_MODE_MINIMAL
    if (*framebuffer_clear_needed) {
        int y;
        int left = viewport_x >> 3;
        int right = (viewport_x + viewport_size + 7) >> 3;
        size_t row_bytes = (size_t)(right - left);

        for (y = viewport_y; y < viewport_y + viewport_size; y++) {
            memset(frame + y * LCD_ROWSIZE + left, 0, row_bytes);
        }
        *framebuffer_clear_needed = false;
    }
    pd->graphics->markUpdatedRows(viewport_y, viewport_y + viewport_size - 1);
    return;
#endif

    for (x = 0; x < 4; x++) {
        luma[x] = palette_luma(w4_read32LE(&w4_mem->palette[x]));
        sorted_luma[x] = luma[x];
    }

    for (x = 0; x < 3; x++) {
        for (y = x + 1; y < 4; y++) {
            if (sorted_luma[x] > sorted_luma[y]) {
                uint8_t tmp = sorted_luma[x];
                sorted_luma[x] = sorted_luma[y];
                sorted_luma[y] = tmp;
            }
        }
    }
    binary_threshold = (uint8_t)(((uint16_t)sorted_luma[1] + (uint16_t)sorted_luma[2]) / 2);
    luma_min = sorted_luma[0];
    luma_range = (uint32_t)(sorted_luma[3] - sorted_luma[0]);
    if (luma_range == 0) {
        luma_range = 1;
    }

    if (render_mode == W4_RENDER_MODE_FAST_160) {
        uint8_t binary_nibble[256];
        uint8_t ordered_nibble[2][256];
        int dst_row_bytes = W4_WIDTH / 8;
        bool cleared = false;

        build_fast160_nibble_tables(luma, binary_threshold, luma_min, luma_range,
                                    binary_nibble, ordered_nibble);

        if (*framebuffer_clear_needed) {
            for (y = 0; y < LCD_ROWS; y++) {
                uint8_t *row = frame + y * LCD_ROWSIZE + (viewport_x >> 3);
                memset(row, 0, (size_t)dst_row_bytes);
            }
            *framebuffer_clear_needed = false;
            cleared = true;
        }

        for (y = 0; y < W4_HEIGHT; y++) {
            const uint8_t *src_row = w4_mem->framebuffer + (size_t)y * (W4_WIDTH / 4);
            uint8_t *dst_row = frame + (viewport_y + y) * LCD_ROWSIZE + (viewport_x >> 3);

            if (dither_mode == W4_DITHER_NONE) {
                for (x = 0; x < dst_row_bytes; x++) {
                    uint8_t b0 = src_row[x * 2];
                    uint8_t b1 = src_row[x * 2 + 1];
                    dst_row[x] = (uint8_t)((binary_nibble[b0] << 4) | binary_nibble[b1]);
                }
            }
            else {
                const uint8_t *tbl = ordered_nibble[y & 1];
                for (x = 0; x < dst_row_bytes; x++) {
                    uint8_t b0 = src_row[x * 2];
                    uint8_t b1 = src_row[x * 2 + 1];
                    dst_row[x] = (uint8_t)((tbl[b0] << 4) | tbl[b1]);
                }
            }
        }

        if (cleared) {
            pd->graphics->markUpdatedRows(0, LCD_ROWS - 1);
        }
        else {
            pd->graphics->markUpdatedRows(viewport_y, viewport_y + W4_HEIGHT - 1);
        }
        return;
    }

    build_threshold_tables(binary_threshold, luma_min, luma_range, binary_black,
                           ordered_black);
    init_scale_map(viewport_size);

    for (pixel_index = 0; pixel_index < (uint32_t)(W4_WIDTH * W4_HEIGHT); pixel_index++) {
        uint8_t quartet = w4_mem->framebuffer[pixel_index >> 2];
        uint8_t color_index = (quartet >> ((pixel_index & 0x3u) << 1)) & 0x3u;
        g_src_luma[pixel_index] = luma[color_index];
    }

    for (y = 0; y < viewport_size; y++) {
        uint16_t src_y = g_scale_y0[y];
        if (g_scale_yw[y] >= 32768u && g_scale_y1[y] > src_y) {
            src_y = g_scale_y1[y];
        }
        const uint8_t *src_row = g_src_luma + (uint32_t)src_y * W4_WIDTH;
        uint8_t *row = frame + (viewport_y + y) * LCD_ROWSIZE;

        for (x = 0; x < viewport_size; x++) {
            uint16_t src_x = g_scale_x0[x];
            if (g_scale_xw[x] >= 32768u && g_scale_x1[x] > src_x) {
                src_x = g_scale_x1[x];
            }
            uint8_t sample_luma = src_row[src_x];
            bool black;
            uint8_t *byte = row + ((viewport_x + x) >> 3);
            uint8_t mask = (uint8_t)(0x80u >> ((viewport_x + x) & 7));

            if (dither_mode == W4_DITHER_NONE) {
                black = binary_black[sample_luma] != 0;
            }
            else {
                int parity = ((y & 1) << 1) | (x & 1);
                black = ordered_black[parity][sample_luma] != 0;
            }
            if (black) {
                *byte |= mask;
            }
            else {
                *byte &= (uint8_t)~mask;
            }
        }
    }

    pd->graphics->markUpdatedRows(viewport_y, viewport_y + viewport_size - 1);
}

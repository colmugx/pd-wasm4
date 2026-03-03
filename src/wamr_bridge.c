#include <ctype.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pd_api.h"
#include "wasm_export.h"

#include "framebuffer.h"
#include "host_audio.h"
#include "util.h"

#define W4_CART_DIR "cart"
#define W4_DEFAULT_CART_PATH "cart/main.wasm"

#define WAMR_STACK_SIZE (64 * 1024)
#define WAMR_HEAP_SIZE (64 * 1024)

#define W4_WIDTH 160
#define W4_HEIGHT 160
#define W4_MEMORY_SIZE (64 * 1024)
#define W4_FRAMEBUFFER_OFFSET 0x00a0
#define W4_FRAMEBUFFER_SIZE (W4_WIDTH * W4_HEIGHT / 4)

#define W4_SYSTEM_PRESERVE_FRAMEBUFFER 1

#define W4_BUTTON_1 1
#define W4_BUTTON_2 2
#define W4_BUTTON_LEFT 16
#define W4_BUTTON_RIGHT 32
#define W4_BUTTON_UP 64
#define W4_BUTTON_DOWN 128

#define W4_MAX_CARTS 64
#define W4_MAX_PATH_LEN 128
#define W4_MAX_ERROR_LEN 256
#define W4_DISK_MAX_BYTES 1024
#define W4_DISK_FLUSH_INTERVAL_FRAMES 30

#define W4_MAX_VIEWPORT_SIZE LCD_ROWS

#ifndef WAMR_PD_AUDIO_CHUNK_FRAMES
#define WAMR_PD_AUDIO_CHUNK_FRAMES 512
#endif

#ifndef WAMR_PD_LOGIC_TICK_DIVIDER
#define WAMR_PD_LOGIC_TICK_DIVIDER 1
#endif

#ifndef WAMR_PD_DISABLE_AUDIO_TICK
#define WAMR_PD_DISABLE_AUDIO_TICK 0
#endif

#ifndef WAMR_PD_COMPOSITE_MODE
#define WAMR_PD_COMPOSITE_MODE 0
#endif

#ifndef WAMR_PD_ENABLE_AOT
#define WAMR_PD_ENABLE_AOT 0
#endif

#ifndef WAMR_PD_USE_POOL_ALLOC
#define WAMR_PD_USE_POOL_ALLOC 0
#endif

#ifndef WAMR_PD_POOL_SIZE_BYTES
#define WAMR_PD_POOL_SIZE_BYTES (256 * 1024)
#endif

#ifndef WAMR_PD_AUDIO_BACKEND
#define WAMR_PD_AUDIO_BACKEND WAMR_PD_AUDIO_BACKEND_NATIVE
#endif

#if WAMR_PD_AUDIO_CHUNK_FRAMES <= 0
#error "WAMR_PD_AUDIO_CHUNK_FRAMES must be > 0"
#endif

#if WAMR_PD_LOGIC_TICK_DIVIDER <= 0
#error "WAMR_PD_LOGIC_TICK_DIVIDER must be > 0"
#endif

#if (WAMR_PD_COMPOSITE_MODE != 0) && (WAMR_PD_COMPOSITE_MODE != 1)
#error "WAMR_PD_COMPOSITE_MODE must be 0 or 1"
#endif

#if (WAMR_PD_DISABLE_AUDIO_TICK != 0) && (WAMR_PD_DISABLE_AUDIO_TICK != 1)
#error "WAMR_PD_DISABLE_AUDIO_TICK must be 0 or 1"
#endif

#if WAMR_PD_POOL_SIZE_BYTES < (128 * 1024)
#error "WAMR_PD_POOL_SIZE_BYTES must be >= 128KB"
#endif

#if (WAMR_PD_AUDIO_BACKEND != WAMR_PD_AUDIO_BACKEND_WASM4_COMPAT) \
    && (WAMR_PD_AUDIO_BACKEND != WAMR_PD_AUDIO_BACKEND_NATIVE)
#error "WAMR_PD_AUDIO_BACKEND must be 0 or 1"
#endif

#define W4_DITHER_NONE 0
#define W4_DITHER_ORDERED 1
#define W4_DITHER_MODE_COUNT 2

#define W4_RENDER_MODE_FAST_160 0
#define W4_RENDER_MODE_QUALITY_240 1
#define W4_RENDER_MODE_COUNT 2
#define W4_COMPOSITE_MODE_NORMAL 0
#define W4_COMPOSITE_MODE_MINIMAL 1
#define W4_LOG_ERROR 0
#define W4_LOG_INFO 1
#define W4_LOG_DEBUG 2
#define W4_REFRESH_RATE_30 0
#define W4_REFRESH_RATE_50 1
#define W4_REFRESH_RATE_UNCAPPED 2
#define W4_BUTTON_QUEUE_SIZE 32

static const char *g_render_mode_labels[W4_RENDER_MODE_COUNT] = {
    "Fast 160",
    "Quality 240",
};

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

typedef struct W4Disk {
    uint16_t size;
    uint8_t data[W4_DISK_MAX_BYTES];
} W4Disk;

typedef struct CartEntry {
    char path[W4_MAX_PATH_LEN];
    char title[W4_MAX_PATH_LEN];
    char stem[W4_MAX_PATH_LEN];
    char wasm_path[W4_MAX_PATH_LEN];
    char aot_path[W4_MAX_PATH_LEN];
} CartEntry;

typedef struct WamrState {
    bool runtime_initialized;
    bool loaded;
#if WAMR_PD_USE_POOL_ALLOC
    uint8_t wamr_pool[WAMR_PD_POOL_SIZE_BYTES];
#endif

    uint8_t *wasm_bytes;
    uint32_t wasm_size;

    wasm_module_t module;
    wasm_module_inst_t module_inst;
    wasm_exec_env_t exec_env;
    wasm_function_inst_t fn_start;
    wasm_function_inst_t fn_update;
    wasm_memory_inst_t memory_inst;

    uint8_t *w4_memory;
    uint32_t w4_memory_size;
    W4MemoryMap *w4_mem;
    bool first_frame;

    W4Disk disk;
    bool disk_dirty;
    uint32_t frames_since_disk_write;
    char disk_path[W4_MAX_PATH_LEN];

    CartEntry carts[W4_MAX_CARTS];
    int cart_count;
    int selected_cart;

    PDMenuItem *menu_reload_item;
    PDMenuItem *menu_reset_item;
    PDMenuItem *menu_render_item;
    const char *render_mode_ptrs[W4_RENDER_MODE_COUNT];
    int dither_mode;
    int render_mode;
    uint32_t logic_tick_counter;

    SoundSource *audio_source;

    float last_load_ms;
    float last_step_ms;
    float last_wasm_update_ms;
    float last_audio_tick_ms;
    float last_composite_ms;
    float last_fps;
    char last_error[W4_MAX_ERROR_LEN];
    bool block_button1_until_release;
    bool framebuffer_clear_needed;
    bool paused;
    bool button_callback_enabled;
    int log_level;
    int refresh_rate_mode;
    float refresh_rate;
    atomic_uint_fast32_t button_down_mask;
    atomic_uint_fast32_t button_pressed_mask;
    atomic_uint_fast32_t button_released_mask;
} WamrState;

static PlaydateAPI *pd;
static WamrState g_state;

static bool load_wasm_cart(const char *path);
static bool load_module_from_path(const char *module_path, const char *cart_path);
static void cleanup_loaded_module(void);
static void refresh_cart_list(const char *preferred_path);
static bool set_dither_mode(int mode);
static bool set_render_mode(int mode);
static void init_scale_map(int viewport_size);
static bool set_refresh_rate_mode(int mode);
static int on_button_event(PDButtons button, int down, uint32_t when, void *userdata);

static void
log_message(int level, const char *fmt, ...)
{
    va_list args;
    char buf[384];
    const char *level_tag;

    if (!pd || !pd->system || !pd->system->logToConsole) {
        return;
    }
    if (level > g_state.log_level) {
        return;
    }

    level_tag = (level <= W4_LOG_ERROR)
        ? "error"
        : ((level == W4_LOG_INFO) ? "info" : "debug");

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    pd->system->logToConsole("[wasm4][%s] %s", level_tag, buf);
}

static void
log_file_error(const char *op, const char *path, const char *message)
{
    log_message(W4_LOG_ERROR, "[file][op=%s][path=%s] %s",
                op ? op : "?", path ? path : "?", message ? message : "unknown");
}

static void
set_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_state.last_error, sizeof(g_state.last_error), fmt, args);
    va_end(args);
    log_message(W4_LOG_ERROR, "%s", g_state.last_error);
}

static void
clear_error(void)
{
    g_state.last_error[0] = '\0';
}

static const char *
current_cart_path(void)
{
    if (g_state.selected_cart >= 0 && g_state.selected_cart < g_state.cart_count) {
        return g_state.carts[g_state.selected_cart].path;
    }
    return W4_DEFAULT_CART_PATH;
}

static int
find_cart_index(const char *path)
{
    int i;

    if (!path) {
        return -1;
    }

    for (i = 0; i < g_state.cart_count; i++) {
        if (strcmp(g_state.carts[i].path, path) == 0
            || (g_state.carts[i].wasm_path[0] != '\0'
                && strcmp(g_state.carts[i].wasm_path, path) == 0)
            || (g_state.carts[i].aot_path[0] != '\0'
                && strcmp(g_state.carts[i].aot_path, path) == 0)) {
            return i;
        }
    }
    return -1;
}

static int
find_cart_index_by_stem(const char *stem)
{
    int i;

    if (!stem || stem[0] == '\0') {
        return -1;
    }

    for (i = 0; i < g_state.cart_count; i++) {
        if (strcmp(g_state.carts[i].stem, stem) == 0) {
            return i;
        }
    }
    return -1;
}

static void
extract_cart_stem(const char *name, char *out, size_t out_size)
{
    const char *dot;
    size_t len;

    if (!name || !out || out_size == 0) {
        return;
    }

    dot = strrchr(name, '.');
    if (!dot || dot == name) {
        strncpy(out, name, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    len = (size_t)(dot - name);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, name, len);
    out[len] = '\0';
}

static const char *
filename_from_path(const char *path)
{
    const char *base;

    if (!path) {
        return "";
    }

    base = strrchr(path, '/');
    return base ? base + 1 : path;
}

static void
normalize_cart_entry(CartEntry *entry)
{
    const char *display_path;
    const char *display_name;

    if (!entry) {
        return;
    }

    if (entry->aot_path[0] != '\0' && entry->wasm_path[0] != '\0') {
        display_path = entry->wasm_path;
        strncpy(entry->path, entry->wasm_path, sizeof(entry->path) - 1);
        entry->path[sizeof(entry->path) - 1] = '\0';
    }
    else if (entry->aot_path[0] != '\0') {
        display_path = entry->aot_path;
        strncpy(entry->path, entry->aot_path, sizeof(entry->path) - 1);
        entry->path[sizeof(entry->path) - 1] = '\0';
    }
    else if (entry->wasm_path[0] != '\0') {
        display_path = entry->wasm_path;
        strncpy(entry->path, entry->wasm_path, sizeof(entry->path) - 1);
        entry->path[sizeof(entry->path) - 1] = '\0';
    }
    else {
        entry->path[0] = '\0';
        entry->title[0] = '\0';
        return;
    }

    display_name = filename_from_path(display_path);
    strncpy(entry->title, display_name, sizeof(entry->title) - 1);
    entry->title[sizeof(entry->title) - 1] = '\0';
}

static int
cart_entry_cmp(const void *a, const void *b)
{
    const CartEntry *ca = (const CartEntry *)a;
    const CartEntry *cb = (const CartEntry *)b;
    return strcmp(ca->title, cb->title);
}

static bool
str_ends_with(const char *s, const char *suffix)
{
    size_t s_len;
    size_t suffix_len;

    if (!s || !suffix) {
        return false;
    }

    s_len = strlen(s);
    suffix_len = strlen(suffix);
    if (s_len < suffix_len) {
        return false;
    }
    return strcmp(s + s_len - suffix_len, suffix) == 0;
}

static void
collect_cart_callback(const char *name, void *userdata)
{
    char full_path[W4_MAX_PATH_LEN];
    char stem[W4_MAX_PATH_LEN];
    FileStat st;
    int idx;
    bool is_wasm;
    bool is_aot;

    (void)userdata;

    if (!name) {
        return;
    }

    is_wasm = str_ends_with(name, ".wasm");
    is_aot = str_ends_with(name, ".aot");
    if (!is_wasm && !is_aot) {
        return;
    }
    if (is_aot && !WAMR_PD_ENABLE_AOT) {
        return;
    }

    if (snprintf(full_path, sizeof(full_path), "%s/%s", W4_CART_DIR, name)
        >= (int)sizeof(full_path)) {
        return;
    }

    if (pd->file->stat(full_path, &st) != 0 || st.isdir) {
        return;
    }

    extract_cart_stem(name, stem, sizeof(stem));
    idx = find_cart_index_by_stem(stem);
    if (idx < 0) {
        idx = g_state.cart_count;
        if (idx >= W4_MAX_CARTS) {
            return;
        }

        memset(&g_state.carts[idx], 0, sizeof(g_state.carts[idx]));
        snprintf(g_state.carts[idx].stem, sizeof(g_state.carts[idx].stem), "%s",
                 stem);
        g_state.cart_count++;
    }

    if (is_aot) {
        snprintf(g_state.carts[idx].aot_path, sizeof(g_state.carts[idx].aot_path),
                 "%s", full_path);
    }
    else {
        snprintf(g_state.carts[idx].wasm_path, sizeof(g_state.carts[idx].wasm_path),
                 "%s", full_path);
    }

    normalize_cart_entry(&g_state.carts[idx]);
}

static void
remove_menu_items(void)
{
    if (g_state.menu_reload_item) {
        pd->system->removeMenuItem(g_state.menu_reload_item);
        g_state.menu_reload_item = NULL;
    }
    if (g_state.menu_reset_item) {
        pd->system->removeMenuItem(g_state.menu_reset_item);
        g_state.menu_reset_item = NULL;
    }
    if (g_state.menu_render_item) {
        pd->system->removeMenuItem(g_state.menu_render_item);
        g_state.menu_render_item = NULL;
    }
}

static void
on_menu_reload(void *userdata)
{
    (void)userdata;
    cleanup_loaded_module();
    clear_error();
}

static void
on_menu_reset(void *userdata)
{
    (void)userdata;
    (void)load_wasm_cart(current_cart_path());
}

static void
on_menu_render(void *userdata)
{
    int mode;

    (void)userdata;

    if (!g_state.menu_render_item) {
        return;
    }

    mode = pd->system->getMenuItemValue(g_state.menu_render_item);
    (void)set_render_mode(mode);
}

static void
setup_menu_items(void)
{
    int i;

    remove_menu_items();

    g_state.menu_reload_item =
        pd->system->addMenuItem("Back To List", on_menu_reload, NULL);
    g_state.menu_reset_item =
        pd->system->addMenuItem("Reset", on_menu_reset, NULL);

    for (i = 0; i < W4_RENDER_MODE_COUNT; i++) {
        g_state.render_mode_ptrs[i] = g_render_mode_labels[i];
    }
    g_state.menu_render_item = pd->system->addOptionsMenuItem(
        "Scale", g_state.render_mode_ptrs, W4_RENDER_MODE_COUNT,
        on_menu_render, NULL);
    if (g_state.menu_render_item) {
        pd->system->setMenuItemValue(g_state.menu_render_item,
                                     g_state.render_mode);
    }
}

static void
refresh_cart_list(const char *preferred_path)
{
    int idx;

    g_state.cart_count = 0;

    if (pd->file->listfiles(W4_CART_DIR, collect_cart_callback, NULL, 0) != 0) {
        log_file_error("listfiles", W4_CART_DIR, pd->file->geterr());
    }

    if (g_state.cart_count == 0) {
        memset(&g_state.carts[0], 0, sizeof(g_state.carts[0]));
        strncpy(g_state.carts[0].stem, "main", sizeof(g_state.carts[0].stem) - 1);
        g_state.carts[0].stem[sizeof(g_state.carts[0].stem) - 1] = '\0';
        strncpy(g_state.carts[0].wasm_path, W4_DEFAULT_CART_PATH,
                sizeof(g_state.carts[0].wasm_path) - 1);
        g_state.carts[0].wasm_path[sizeof(g_state.carts[0].wasm_path) - 1] = '\0';
        normalize_cart_entry(&g_state.carts[0]);
        g_state.cart_count = 1;
    }
    else {
        qsort(g_state.carts, (size_t)g_state.cart_count, sizeof(g_state.carts[0]),
              cart_entry_cmp);
    }

    idx = find_cart_index(preferred_path);
    if (idx >= 0) {
        g_state.selected_cart = idx;
    }
    else if (g_state.selected_cart < 0 || g_state.selected_cart >= g_state.cart_count) {
        g_state.selected_cart = 0;
    }

    setup_menu_items();
}

static void
set_host_exception(wasm_exec_env_t exec_env, const char *message)
{
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);

    if (inst) {
        wasm_runtime_set_exception(inst, message);
    }
    set_error("host exception: %s", message);
}

static bool
bounds_check_raw(const void *ptr, size_t size)
{
    const uint8_t *mem_start;
    const uint8_t *mem_end;
    const uint8_t *p;

    if (!g_state.w4_memory || g_state.w4_memory_size == 0) {
        return false;
    }

    mem_start = g_state.w4_memory;
    mem_end = mem_start + g_state.w4_memory_size;
    p = (const uint8_t *)ptr;

    if (p < mem_start) {
        return false;
    }
    if (size > (size_t)(mem_end - p)) {
        return false;
    }
    return true;
}

static bool
bounds_check_cstr_raw(const char *str)
{
    const uint8_t *mem_start;
    const uint8_t *mem_end;
    const uint8_t *p;

    if (!g_state.w4_memory || g_state.w4_memory_size == 0 || !str) {
        return false;
    }

    mem_start = g_state.w4_memory;
    mem_end = mem_start + g_state.w4_memory_size;
    p = (const uint8_t *)str;

    if (p < mem_start || p >= mem_end) {
        return false;
    }

    while (p < mem_end) {
        if (*p == '\0') {
            return true;
        }
        p++;
    }

    return false;
}

static bool
bounds_check(wasm_exec_env_t exec_env, const void *ptr, size_t size)
{
    if (!bounds_check_raw(ptr, size)) {
        set_host_exception(exec_env, "out of bounds memory access");
        return false;
    }
    return true;
}

static bool
bounds_check_cstr(wasm_exec_env_t exec_env, const char *str)
{
    if (!bounds_check_cstr_raw(str)) {
        set_host_exception(exec_env, "out of bounds string access");
        return false;
    }
    return true;
}

static uint8_t
palette_luma(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xff;
    uint32_t g = (rgb >> 8) & 0xff;
    uint32_t b = rgb & 0xff;

    return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

static void
get_viewport_rect(int *x, int *y, int *size)
{
    int viewport_size = (g_state.render_mode == W4_RENDER_MODE_FAST_160)
                            ? W4_WIDTH
                            : LCD_ROWS;

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

static void
composite_to_playdate_framebuffer(void)
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

    if (!g_state.w4_mem) {
        return;
    }

    frame = pd->graphics->getFrame();
    if (!frame) {
        return;
    }
    get_viewport_rect(&viewport_x, &viewport_y, &viewport_size);

#if WAMR_PD_COMPOSITE_MODE == W4_COMPOSITE_MODE_MINIMAL
    if (g_state.framebuffer_clear_needed) {
        int y;
        int left = viewport_x >> 3;
        int right = (viewport_x + viewport_size + 7) >> 3;
        size_t row_bytes = (size_t)(right - left);

        for (y = viewport_y; y < viewport_y + viewport_size; y++) {
            memset(frame + y * LCD_ROWSIZE + left, 0, row_bytes);
        }
        g_state.framebuffer_clear_needed = false;
    }
    pd->graphics->markUpdatedRows(viewport_y, viewport_y + viewport_size - 1);
    return;
#endif

    for (x = 0; x < 4; x++) {
        luma[x] = palette_luma(w4_read32LE(&g_state.w4_mem->palette[x]));
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

    if (g_state.render_mode == W4_RENDER_MODE_FAST_160) {
        uint8_t binary_nibble[256];
        uint8_t ordered_nibble[2][256];
        int dst_row_bytes = W4_WIDTH / 8;
        bool cleared = false;

        build_fast160_nibble_tables(luma, binary_threshold, luma_min, luma_range,
                                    binary_nibble, ordered_nibble);

        if (g_state.framebuffer_clear_needed) {
            for (y = 0; y < LCD_ROWS; y++) {
                uint8_t *row = frame + y * LCD_ROWSIZE + (viewport_x >> 3);
                memset(row, 0, (size_t)dst_row_bytes);
            }
            g_state.framebuffer_clear_needed = false;
            cleared = true;
        }

        for (y = 0; y < W4_HEIGHT; y++) {
            const uint8_t *src_row =
                g_state.w4_mem->framebuffer + (size_t)y * (W4_WIDTH / 4);
            uint8_t *dst_row =
                frame + (viewport_y + y) * LCD_ROWSIZE + (viewport_x >> 3);

            if (g_state.dither_mode == W4_DITHER_NONE) {
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

    for (pixel_index = 0; pixel_index < (uint32_t)(W4_WIDTH * W4_HEIGHT);
         pixel_index++) {
        uint8_t quartet = g_state.w4_mem->framebuffer[pixel_index >> 2];
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

            if (g_state.dither_mode == W4_DITHER_NONE) {
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

static void
init_w4_memory_state(void)
{
    memset(g_state.w4_memory, 0, W4_FRAMEBUFFER_OFFSET + W4_FRAMEBUFFER_SIZE);

    w4_write32LE(&g_state.w4_mem->palette[0], 0xe0f8cf);
    w4_write32LE(&g_state.w4_mem->palette[1], 0x86c06c);
    w4_write32LE(&g_state.w4_mem->palette[2], 0x306850);
    w4_write32LE(&g_state.w4_mem->palette[3], 0x071821);

    g_state.w4_mem->drawColors[0] = 0x03;
    g_state.w4_mem->drawColors[1] = 0x12;

    w4_write16LE(&g_state.w4_mem->mouseX, 0x7fff);
    w4_write16LE(&g_state.w4_mem->mouseY, 0x7fff);
    g_state.w4_mem->mouseButtons = 0;

    g_state.w4_mem->gamepads[0] = 0;
    g_state.w4_mem->gamepads[1] = 0;
    g_state.w4_mem->gamepads[2] = 0;
    g_state.w4_mem->gamepads[3] = 0;
    g_state.w4_mem->systemFlags = 0;

    w4_framebufferInit(g_state.w4_mem->drawColors, g_state.w4_mem->framebuffer);
    host_audio_reset();

    g_state.first_frame = true;
    g_state.framebuffer_clear_needed = true;
}

static void
derive_disk_path(const char *cart_path, char *out_path, size_t out_size)
{
    const char *base;
    char stem[64];
    size_t i = 0;

    base = strrchr(cart_path, '/');
    base = base ? base + 1 : cart_path;

    while (base[i] != '\0' && base[i] != '.' && i + 1 < sizeof(stem)) {
        char c = base[i];
        if (!isalnum((unsigned char)c) && c != '-' && c != '_') {
            c = '_';
        }
        stem[i] = c;
        i++;
    }
    stem[i] = '\0';

    if (stem[0] == '\0') {
        strncpy(stem, "cart", sizeof(stem) - 1);
        stem[sizeof(stem) - 1] = '\0';
    }

    if (pd->file->mkdir("save") != 0) {
        const char *err = pd->file->geterr();
        if (err && err[0] != '\0') {
            log_file_error("mkdir", "save", err);
        }
    }
    snprintf(out_path, out_size, "save/%s.disk", stem);
}

static void
load_disk_for_cart(const char *cart_path)
{
    SDFile *file;
    int bytes_read;

    memset(&g_state.disk, 0, sizeof(g_state.disk));
    g_state.disk_dirty = false;
    g_state.frames_since_disk_write = 0;

    derive_disk_path(cart_path, g_state.disk_path, sizeof(g_state.disk_path));

    file = pd->file->open(g_state.disk_path, kFileReadData);
    if (!file) {
        file = pd->file->open(g_state.disk_path, kFileRead);
    }
    if (!file) {
        const char *err = pd->file->geterr();
        if (err && err[0] != '\0') {
            log_file_error("open", g_state.disk_path, err);
        }
        return;
    }

    bytes_read = pd->file->read(file, g_state.disk.data, sizeof(g_state.disk.data));
    if (bytes_read < 0) {
        log_file_error("read", g_state.disk_path, pd->file->geterr());
    }
    if (bytes_read > 0) {
        g_state.disk.size = (uint16_t)bytes_read;
    }

    pd->file->close(file);
}

static bool
flush_disk_if_needed(bool force)
{
    SDFile *file;
    int written;

    if (!g_state.disk_dirty) {
        return true;
    }

    if (!force && g_state.frames_since_disk_write < W4_DISK_FLUSH_INTERVAL_FRAMES) {
        return true;
    }

    if (pd->file->mkdir("save") != 0) {
        const char *err = pd->file->geterr();
        if (err && err[0] != '\0') {
            log_file_error("mkdir", "save", err);
        }
    }

    if (g_state.disk.size == 0) {
        if (pd->file->unlink(g_state.disk_path, 0) != 0) {
            log_file_error("unlink", g_state.disk_path, pd->file->geterr());
            return false;
        }
        g_state.disk_dirty = false;
        g_state.frames_since_disk_write = 0;
        return true;
    }

    file = pd->file->open(g_state.disk_path, kFileWrite);
    if (!file) {
        log_file_error("open", g_state.disk_path, pd->file->geterr());
        set_error("open disk failed for %s: %s", g_state.disk_path,
                  pd->file->geterr());
        return false;
    }

    written = pd->file->write(file, g_state.disk.data, g_state.disk.size);
    pd->file->close(file);

    if (written != g_state.disk.size) {
        log_file_error("write", g_state.disk_path, pd->file->geterr());
        set_error("write disk failed for %s", g_state.disk_path);
        return false;
    }

    g_state.disk_dirty = false;
    g_state.frames_since_disk_write = 0;
    return true;
}

static bool
read_file_into_memory(const char *path, uint8_t **out_buf, uint32_t *out_size)
{
    FileStat st;
    SDFile *file;
    uint8_t *buf;
    uint32_t total_read = 0;
    int read_count;

    if (!path || !out_buf || !out_size) {
        set_error("invalid read_file arguments");
        return false;
    }

    if (pd->file->stat(path, &st) != 0) {
        log_file_error("stat", path, pd->file->geterr());
        set_error("stat failed for %s: %s", path, pd->file->geterr());
        return false;
    }

    if (st.isdir) {
        set_error("%s is a directory", path);
        return false;
    }

    if (st.size == 0) {
        set_error("%s is empty", path);
        return false;
    }

    file = pd->file->open(path, kFileReadData);
    if (!file) {
        file = pd->file->open(path, kFileRead);
    }
    if (!file) {
        log_file_error("open", path, pd->file->geterr());
        set_error("open failed for %s: %s", path, pd->file->geterr());
        return false;
    }

    buf = malloc(st.size);
    if (!buf) {
        pd->file->close(file);
        set_error("malloc failed for %u bytes", st.size);
        return false;
    }

    while (total_read < st.size) {
        read_count = pd->file->read(file, buf + total_read, st.size - total_read);
        if (read_count <= 0) {
            break;
        }
        total_read += (uint32_t)read_count;
    }

    pd->file->close(file);

    if (total_read != st.size) {
        log_file_error("read", path, pd->file->geterr());
        free(buf);
        set_error("read failed for %s (%u/%u)", path, total_read, st.size);
        return false;
    }

    *out_buf = buf;
    *out_size = st.size;
    return true;
}

static int
audio_source_callback(void *context, int16_t *left, int16_t *right, int len)
{
#if WAMR_PD_DISABLE_AUDIO_TICK
    (void)context;

    if (!left || !right || len <= 0) {
        return 0;
    }

    memset(left, 0, sizeof(int16_t) * (size_t)len);
    memset(right, 0, sizeof(int16_t) * (size_t)len);
    return 1;
#else
    bool output_enabled;

    (void)context;

    if (!left || !right || len <= 0) {
        return 0;
    }

    output_enabled = g_state.loaded && !g_state.paused;
    return host_audio_render(left, right, len, output_enabled);
#endif
}

static bool
call_void_export(wasm_function_inst_t fn)
{
    if (!fn) {
        return true;
    }

    if (!wasm_runtime_call_wasm(g_state.exec_env, fn, 0, NULL)) {
        const char *exception = wasm_runtime_get_exception(g_state.module_inst);
        set_error("wasm exception: %s", exception ? exception : "unknown");
        return false;
    }

    return true;
}

static void
cleanup_loaded_module(void)
{
    (void)flush_disk_if_needed(true);

    if (g_state.exec_env) {
        wasm_runtime_destroy_exec_env(g_state.exec_env);
        g_state.exec_env = NULL;
    }

    if (g_state.module_inst) {
        wasm_runtime_deinstantiate(g_state.module_inst);
        g_state.module_inst = NULL;
    }

    if (g_state.module) {
        wasm_runtime_unload(g_state.module);
        g_state.module = NULL;
    }

    if (g_state.wasm_bytes) {
        free(g_state.wasm_bytes);
        g_state.wasm_bytes = NULL;
    }

    g_state.wasm_size = 0;
    g_state.fn_start = NULL;
    g_state.fn_update = NULL;
    g_state.memory_inst = NULL;
    g_state.w4_memory = NULL;
    g_state.w4_memory_size = 0;
    g_state.w4_mem = NULL;
    g_state.first_frame = false;
    g_state.framebuffer_clear_needed = true;

    g_state.loaded = false;
    g_state.paused = false;
    host_audio_set_paused(false);
    host_audio_reset();
    if (g_state.button_callback_enabled && pd && pd->system
        && pd->system->setButtonCallback) {
        pd->system->setButtonCallback(NULL, NULL, 0);
    }
    g_state.button_callback_enabled = false;
    g_state.last_step_ms = 0.0f;
    g_state.last_wasm_update_ms = 0.0f;
    g_state.last_audio_tick_ms = 0.0f;
    g_state.last_composite_ms = 0.0f;
    g_state.last_fps = 0.0f;
    g_state.logic_tick_counter = 0;
    atomic_store_explicit(&g_state.button_down_mask, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_state.button_pressed_mask, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_state.button_released_mask, 0u, memory_order_relaxed);
}

static bool
host_blit_sub_common(wasm_exec_env_t exec_env, const uint8_t *sprite, int32_t x,
                     int32_t y, int32_t width, int32_t height, int32_t src_x,
                     int32_t src_y, int32_t stride, int32_t flags)
{
    bool bpp2;
    uint64_t end_index;
    uint64_t bits;
    size_t bytes;

    if (width <= 0 || height <= 0) {
        return true;
    }

    if (src_x < 0 || src_y < 0 || stride <= 0) {
        set_host_exception(exec_env, "invalid blitSub args");
        return false;
    }

    if ((int64_t)src_x + (int64_t)width > (int64_t)stride) {
        set_host_exception(exec_env, "blitSub source rect exceeds stride");
        return false;
    }

    bpp2 = (flags & 1) != 0;
    end_index = (uint64_t)(uint32_t)(src_y + height - 1) * (uint64_t)(uint32_t)stride
                + (uint64_t)(uint32_t)(src_x + width - 1);
    bits = (end_index + 1) * (uint64_t)(bpp2 ? 2 : 1);
    bytes = (size_t)((bits + 7) / 8);

    if (!bounds_check(exec_env, sprite, bytes)) {
        return false;
    }

    w4_framebufferBlit(sprite, x, y, width, height, src_x, src_y, stride, bpp2,
                       (flags & 2) != 0, (flags & 4) != 0, (flags & 8) != 0);
    return true;
}

static void
host_blit(wasm_exec_env_t exec_env, const uint8_t *sprite, int32_t x, int32_t y,
          int32_t width, int32_t height, int32_t flags)
{
    (void)host_blit_sub_common(exec_env, sprite, x, y, width, height, 0, 0,
                               width, flags);
}

static void
host_blit_sub(wasm_exec_env_t exec_env, const uint8_t *sprite, int32_t x,
              int32_t y, int32_t width, int32_t height, int32_t src_x,
              int32_t src_y, int32_t stride, int32_t flags)
{
    (void)host_blit_sub_common(exec_env, sprite, x, y, width, height, src_x,
                               src_y, stride, flags);
}

static void
host_line(wasm_exec_env_t exec_env, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    (void)exec_env;
    w4_framebufferLine(x1, y1, x2, y2);
}

static void
host_hline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t len)
{
    (void)exec_env;
    w4_framebufferHLine(x, y, len);
}

static void
host_vline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t len)
{
    (void)exec_env;
    w4_framebufferVLine(x, y, len);
}

static void
host_oval(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t width,
          int32_t height)
{
    (void)exec_env;
    w4_framebufferOval(x, y, width, height);
}

static void
host_rect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t width,
          int32_t height)
{
    (void)exec_env;
    w4_framebufferRect(x, y, width, height);
}

static void
host_text(wasm_exec_env_t exec_env, const char *str, int32_t x, int32_t y)
{
    if (!bounds_check_cstr(exec_env, str)) {
        return;
    }
    w4_framebufferText((const uint8_t *)str, x, y);
}

static void
host_text_utf8(wasm_exec_env_t exec_env, const uint8_t *str, int32_t byte_length,
               int32_t x, int32_t y)
{
    if (byte_length < 0) {
        set_host_exception(exec_env, "negative byteLength");
        return;
    }
    if (!bounds_check(exec_env, str, (size_t)byte_length)) {
        return;
    }
    w4_framebufferTextUtf8(str, byte_length, x, y);
}

static void
host_text_utf16(wasm_exec_env_t exec_env, const uint16_t *str,
                int32_t byte_length, int32_t x, int32_t y)
{
    if (byte_length < 0) {
        set_host_exception(exec_env, "negative byteLength");
        return;
    }
    if (!bounds_check(exec_env, str, (size_t)byte_length)) {
        return;
    }
    w4_framebufferTextUtf16(str, byte_length, x, y);
}

static void
host_tone(wasm_exec_env_t exec_env, int32_t frequency, int32_t duration,
          int32_t volume, int32_t flags)
{
    (void)exec_env;
    host_audio_tone(frequency, duration, volume, flags);
}

static int32_t
host_diskr(wasm_exec_env_t exec_env, uint8_t *dest, int32_t size)
{
    int32_t n;

    if (size < 0) {
        set_host_exception(exec_env, "negative disk size");
        return 0;
    }

    if (!bounds_check(exec_env, dest, (size_t)size)) {
        return 0;
    }

    n = size;
    if (n > g_state.disk.size) {
        n = g_state.disk.size;
    }

    memcpy(dest, g_state.disk.data, (size_t)n);
    return n;
}

static int32_t
host_diskw(wasm_exec_env_t exec_env, const uint8_t *src, int32_t size)
{
    int32_t n;

    if (size < 0) {
        set_host_exception(exec_env, "negative disk size");
        return 0;
    }

    if (!bounds_check(exec_env, src, (size_t)size)) {
        return 0;
    }

    n = size;
    if (n > W4_DISK_MAX_BYTES) {
        n = W4_DISK_MAX_BYTES;
    }

    g_state.disk.size = (uint16_t)n;
    memcpy(g_state.disk.data, src, (size_t)n);
    g_state.disk_dirty = true;
    g_state.frames_since_disk_write = 0;

    return n;
}

static void
host_trace(wasm_exec_env_t exec_env, const char *str)
{
    if (!bounds_check_cstr(exec_env, str)) {
        return;
    }
    log_message(W4_LOG_INFO, "%s", str);
}

static void
host_trace_utf8(wasm_exec_env_t exec_env, const uint8_t *str, int32_t byte_length)
{
    char *buf;
    int32_t n;

    if (byte_length < 0) {
        set_host_exception(exec_env, "negative byteLength");
        return;
    }

    if (!bounds_check(exec_env, str, (size_t)byte_length)) {
        return;
    }

    n = byte_length;
    if (n > 2048) {
        n = 2048;
    }

    buf = malloc((size_t)n + 1);
    if (!buf) {
        return;
    }

    memcpy(buf, str, (size_t)n);
    buf[n] = '\0';
    log_message(W4_LOG_INFO, "%s", buf);
    free(buf);
}

static void
host_trace_utf16(wasm_exec_env_t exec_env, const uint16_t *str,
                 int32_t byte_length)
{
    if (byte_length < 0) {
        set_host_exception(exec_env, "negative byteLength");
        return;
    }

    if (!bounds_check(exec_env, str, (size_t)byte_length)) {
        return;
    }

    log_message(W4_LOG_INFO, "traceUtf16(len=%d)", (int)byte_length);
}

static void
append_char(char *out, size_t *len, size_t cap, char c)
{
    if (*len + 1 >= cap) {
        return;
    }
    out[*len] = c;
    (*len)++;
}

static void
append_cstr(char *out, size_t *len, size_t cap, const char *src)
{
    while (*src) {
        append_char(out, len, cap, *src);
        src++;
    }
}

static void
append_fmt(char *out, size_t *len, size_t cap, const char *fmt, ...)
{
    va_list args;
    int n;

    if (*len >= cap) {
        return;
    }

    va_start(args, fmt);
    n = vsnprintf(out + *len, cap - *len, fmt, args);
    va_end(args);

    if (n < 0) {
        return;
    }

    if ((size_t)n >= cap - *len) {
        *len = cap - 1;
    }
    else {
        *len += (size_t)n;
    }
}

static void
host_tracef(wasm_exec_env_t exec_env, const char *fmt, const void *stack)
{
    const uint8_t *arg_ptr = (const uint8_t *)stack;
    char out[512];
    size_t out_len = 0;

    if (!bounds_check_cstr(exec_env, fmt)) {
        return;
    }

    while (*fmt) {
        if (*fmt != '%') {
            append_char(out, &out_len, sizeof(out), *fmt++);
            continue;
        }

        fmt++;
        if (*fmt == '\0') {
            break;
        }

        switch (*fmt) {
            case '%':
                append_char(out, &out_len, sizeof(out), '%');
                break;
            case 'c':
                if (!bounds_check(exec_env, arg_ptr, 4)) {
                    return;
                }
                append_char(out, &out_len, sizeof(out), (char)w4_read32LE(arg_ptr));
                arg_ptr += 4;
                break;
            case 'd':
                if (!bounds_check(exec_env, arg_ptr, 4)) {
                    return;
                }
                append_fmt(out, &out_len, sizeof(out), "%d",
                           (int32_t)w4_read32LE(arg_ptr));
                arg_ptr += 4;
                break;
            case 'x':
                if (!bounds_check(exec_env, arg_ptr, 4)) {
                    return;
                }
                append_fmt(out, &out_len, sizeof(out), "%x",
                           (uint32_t)w4_read32LE(arg_ptr));
                arg_ptr += 4;
                break;
            case 's': {
                uint32_t ptr;
                const char *s;

                if (!bounds_check(exec_env, arg_ptr, 4)) {
                    return;
                }
                ptr = w4_read32LE(arg_ptr);
                arg_ptr += 4;

                if (ptr >= g_state.w4_memory_size) {
                    set_host_exception(exec_env, "tracef string pointer out of bounds");
                    return;
                }

                s = (const char *)(g_state.w4_memory + ptr);
                if (!bounds_check_cstr(exec_env, s)) {
                    return;
                }

                append_cstr(out, &out_len, sizeof(out), s);
                break;
            }
            case 'f':
                if (!bounds_check(exec_env, arg_ptr, 8)) {
                    return;
                }
                append_fmt(out, &out_len, sizeof(out), "%g", w4_readf64LE(arg_ptr));
                arg_ptr += 8;
                break;
            default:
                append_char(out, &out_len, sizeof(out), '%');
                append_char(out, &out_len, sizeof(out), *fmt);
                break;
        }

        fmt++;
    }

    out[out_len] = '\0';
    log_message(W4_LOG_INFO, "%s", out);
}

static NativeSymbol g_native_symbols[] = {
    { "blit", host_blit, "(*iiiii)", NULL },
    { "blitSub", host_blit_sub, "(*iiiiiiii)", NULL },
    { "line", host_line, "(iiii)", NULL },
    { "hline", host_hline, "(iii)", NULL },
    { "vline", host_vline, "(iii)", NULL },
    { "oval", host_oval, "(iiii)", NULL },
    { "rect", host_rect, "(iiii)", NULL },
    { "text", host_text, "($ii)", NULL },
    { "textUtf8", host_text_utf8, "(*~ii)", NULL },
    { "textUtf16", host_text_utf16, "(*~ii)", NULL },
    { "tone", host_tone, "(iiii)", NULL },
    { "diskr", host_diskr, "(*~)i", NULL },
    { "diskw", host_diskw, "(*~)i", NULL },
    { "trace", host_trace, "($)", NULL },
    { "traceUtf8", host_trace_utf8, "(*~)", NULL },
    { "traceUtf16", host_trace_utf16, "(*~)", NULL },
    { "tracef", host_tracef, "($*)", NULL },
};

static bool
set_dither_mode(int mode)
{
    if (mode < 0 || mode >= W4_DITHER_MODE_COUNT) {
        return false;
    }

    g_state.dither_mode = mode;
    return true;
}

static bool
set_render_mode(int mode)
{
    if (mode < 0 || mode >= W4_RENDER_MODE_COUNT) {
        return false;
    }

    if (g_state.render_mode == mode) {
        return true;
    }

    g_state.render_mode = mode;
    g_state.framebuffer_clear_needed = true;
    if (g_state.menu_render_item) {
        pd->system->setMenuItemValue(g_state.menu_render_item, mode);
    }
    return true;
}

static bool
set_refresh_rate_mode(int mode)
{
    float rate;

    if (mode < W4_REFRESH_RATE_30 || mode > W4_REFRESH_RATE_UNCAPPED) {
        return false;
    }

    switch (mode) {
        case W4_REFRESH_RATE_30:
            rate = 30.0f;
            break;
        case W4_REFRESH_RATE_50:
            rate = 50.0f;
            break;
        case W4_REFRESH_RATE_UNCAPPED:
        default:
            rate = 0.0f;
            break;
    }

    g_state.refresh_rate_mode = mode;
    g_state.refresh_rate = rate;
    if (pd && pd->display && pd->display->setRefreshRate) {
        pd->display->setRefreshRate(rate);
    }
    return true;
}

static int
on_button_event(PDButtons button, int down, uint32_t when, void *userdata)
{
    uint32_t bit = (uint32_t)button;

    (void)when;
    (void)userdata;

    if (down) {
        (void)atomic_fetch_or_explicit(&g_state.button_down_mask, bit,
                                       memory_order_relaxed);
        (void)atomic_fetch_or_explicit(&g_state.button_pressed_mask, bit,
                                       memory_order_relaxed);
    }
    else {
        (void)atomic_fetch_and_explicit(&g_state.button_down_mask, ~bit,
                                        memory_order_relaxed);
        (void)atomic_fetch_or_explicit(&g_state.button_released_mask, bit,
                                       memory_order_relaxed);
    }

    return 0;
}

static bool
ensure_runtime_initialized(void)
{
    RuntimeInitArgs init_args;

    if (g_state.runtime_initialized) {
        return true;
    }

    memset(&init_args, 0, sizeof(init_args));
#if WAMR_PD_USE_POOL_ALLOC
    memset(g_state.wamr_pool, 0, sizeof(g_state.wamr_pool));
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = g_state.wamr_pool;
    init_args.mem_alloc_option.pool.heap_size =
        (unsigned int)sizeof(g_state.wamr_pool);
#else
    init_args.mem_alloc_type = Alloc_With_System_Allocator;
#endif
    init_args.native_module_name = "env";
    init_args.native_symbols = g_native_symbols;
    init_args.n_native_symbols =
        (uint32_t)(sizeof(g_native_symbols) / sizeof(g_native_symbols[0]));

    if (!wasm_runtime_full_init(&init_args)) {
        set_error("wasm_runtime_full_init failed");
        return false;
    }

    g_state.runtime_initialized = true;
    host_audio_init();

    if (!g_state.audio_source) {
        g_state.audio_source = pd->sound->addSource(audio_source_callback, NULL, 1);
        if (!g_state.audio_source) {
            log_message(W4_LOG_ERROR, "failed to create audio source");
        }
    }

    return true;
}

static bool
load_module_from_path(const char *module_path, const char *cart_path)
{
    char error_buf[256] = { 0 };
    uint64_t page_count;
    uint64_t bytes_per_page;
    uint64_t mem_size;

    if (!module_path || module_path[0] == '\0') {
        set_error("invalid module path");
        return false;
    }

    if (!read_file_into_memory(module_path, &g_state.wasm_bytes, &g_state.wasm_size)) {
        return false;
    }

    if (!ensure_runtime_initialized()) {
        cleanup_loaded_module();
        return false;
    }

    g_state.module = wasm_runtime_load(g_state.wasm_bytes, g_state.wasm_size,
                                       error_buf, sizeof(error_buf));
    if (!g_state.module) {
        set_error("module load failed for %s: %s", module_path, error_buf);
        cleanup_loaded_module();
        return false;
    }

    g_state.module_inst = wasm_runtime_instantiate(g_state.module, WAMR_STACK_SIZE,
                                                   WAMR_HEAP_SIZE, error_buf,
                                                   sizeof(error_buf));
    if (!g_state.module_inst) {
        set_error("wasm instantiate failed: %s", error_buf);
        cleanup_loaded_module();
        return false;
    }

    g_state.exec_env =
        wasm_runtime_create_exec_env(g_state.module_inst, WAMR_STACK_SIZE);
    if (!g_state.exec_env) {
        set_error("wasm create exec env failed");
        cleanup_loaded_module();
        return false;
    }

    g_state.fn_start = wasm_runtime_lookup_function(g_state.module_inst, "start");
    g_state.fn_update = wasm_runtime_lookup_function(g_state.module_inst, "update");

    if (!g_state.fn_update) {
        set_error("missing export: update");
        cleanup_loaded_module();
        return false;
    }

    g_state.memory_inst = wasm_runtime_get_default_memory(g_state.module_inst);
    if (!g_state.memory_inst) {
        set_error("missing default memory");
        cleanup_loaded_module();
        return false;
    }

    g_state.w4_memory = (uint8_t *)wasm_memory_get_base_address(g_state.memory_inst);
    if (!g_state.w4_memory) {
        set_error("failed to get memory base");
        cleanup_loaded_module();
        return false;
    }

    page_count = wasm_memory_get_cur_page_count(g_state.memory_inst);
    bytes_per_page = wasm_memory_get_bytes_per_page(g_state.memory_inst);
    mem_size = page_count * bytes_per_page;

    if (mem_size < W4_MEMORY_SIZE) {
        set_error("linear memory too small: %u", (uint32_t)mem_size);
        cleanup_loaded_module();
        return false;
    }

    g_state.w4_memory_size = (uint32_t)mem_size;
    g_state.w4_mem = (W4MemoryMap *)g_state.w4_memory;

    init_w4_memory_state();
    load_disk_for_cart(cart_path ? cart_path : module_path);

    g_state.loaded = true;
    g_state.paused = false;
    host_audio_set_paused(false);
    if (pd->system && pd->system->setButtonCallback) {
        pd->system->setButtonCallback(on_button_event, NULL, W4_BUTTON_QUEUE_SIZE);
        g_state.button_callback_enabled = true;
    }
    g_state.block_button1_until_release = true;
    g_state.last_step_ms = 0.0f;
    g_state.last_wasm_update_ms = 0.0f;
    g_state.last_audio_tick_ms = 0.0f;
    g_state.last_composite_ms = 0.0f;
    g_state.last_fps = 0.0f;
    g_state.logic_tick_counter = 0;
    atomic_store_explicit(&g_state.button_pressed_mask, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_state.button_released_mask, 0u, memory_order_relaxed);

    clear_error();
    return true;
}

static bool
load_wasm_cart(const char *path)
{
    const char *primary_path = NULL;
    const char *fallback_path = NULL;
    const char *cart_path = NULL;
    char primary_error[W4_MAX_ERROR_LEN];
    uint32_t start_ms;
    uint32_t end_ms;
    int idx;

    if (!path || path[0] == '\0') {
        path = current_cart_path();
    }

    idx = find_cart_index(path);
    if (idx >= 0) {
        CartEntry *entry = &g_state.carts[idx];

        g_state.selected_cart = idx;
        cart_path = entry->path;
        if (entry->aot_path[0] != '\0') {
            primary_path = entry->aot_path;
            if (entry->wasm_path[0] != '\0') {
                fallback_path = entry->wasm_path;
            }
        }
        else if (entry->wasm_path[0] != '\0') {
            primary_path = entry->wasm_path;
        }
    }

    if (!primary_path) {
        primary_path = path;
    }
    if (!cart_path) {
        cart_path = path;
    }

    cleanup_loaded_module();
    clear_error();
    primary_error[0] = '\0';
    start_ms = pd->system->getCurrentTimeMilliseconds();

    if (load_module_from_path(primary_path, cart_path)) {
        goto done;
    }

    strncpy(primary_error, g_state.last_error, sizeof(primary_error) - 1);
    primary_error[sizeof(primary_error) - 1] = '\0';

    if (fallback_path && strcmp(fallback_path, primary_path) != 0) {
        cleanup_loaded_module();
        clear_error();
        if (load_module_from_path(fallback_path, cart_path)) {
            log_message(W4_LOG_INFO, "AOT load failed for %s, fallback to wasm: %s",
                        primary_path, primary_error);
            goto done;
        }
        set_error("primary failed: %s; fallback failed: %s", primary_error,
                  g_state.last_error);
        cleanup_loaded_module();
        return false;
    }

    set_error("%s", primary_error);
    cleanup_loaded_module();
    return false;

done:
    end_ms = pd->system->getCurrentTimeMilliseconds();
    g_state.last_load_ms = (float)(end_ms - start_ms);
    return true;
}

static void
update_gamepad_state(void)
{
    PDButtons current;
    PDButtons pushed;
    PDButtons released;
    uint32_t cb_current;
    uint32_t cb_pushed;
    uint32_t cb_released;
    uint8_t gamepad = 0;

    if (!g_state.w4_mem) {
        return;
    }

    pd->system->getButtonState(&current, &pushed, &released);
    if (g_state.button_callback_enabled) {
        cb_current =
            atomic_load_explicit(&g_state.button_down_mask, memory_order_relaxed);
        cb_pushed = atomic_exchange_explicit(&g_state.button_pressed_mask, 0u,
                                             memory_order_relaxed);
        cb_released = atomic_exchange_explicit(&g_state.button_released_mask, 0u,
                                               memory_order_relaxed);
        current = (PDButtons)((uint32_t)current | cb_current);
        pushed = (PDButtons)((uint32_t)pushed | cb_pushed);
        released = (PDButtons)((uint32_t)released | cb_released);
    }
    (void)pushed;

    if (g_state.block_button1_until_release) {
        if ((released & kButtonA) || !(current & kButtonA)) {
            g_state.block_button1_until_release = false;
        }
    }

    if ((current & kButtonA) && !g_state.block_button1_until_release) {
        gamepad |= W4_BUTTON_1;
    }
    if (current & kButtonB) {
        gamepad |= W4_BUTTON_2;
    }
    if (current & kButtonLeft) {
        gamepad |= W4_BUTTON_LEFT;
    }
    if (current & kButtonRight) {
        gamepad |= W4_BUTTON_RIGHT;
    }
    if (current & kButtonUp) {
        gamepad |= W4_BUTTON_UP;
    }
    if (current & kButtonDown) {
        gamepad |= W4_BUTTON_DOWN;
    }

    g_state.w4_mem->gamepads[0] = gamepad;
    g_state.w4_mem->gamepads[1] = 0;
    g_state.w4_mem->gamepads[2] = 0;
    g_state.w4_mem->gamepads[3] = 0;
}

static bool
step_wasm_cart(void)
{
    bool ok = false;
    bool run_logic;
    bool did_start = false;
    uint32_t start_ms;
    uint32_t phase_start_ms;
    uint32_t phase_end_ms;
    uint32_t end_ms;

    if (!g_state.loaded || !g_state.fn_update) {
        set_error("no wasm loaded");
        return false;
    }
    if (g_state.paused) {
        g_state.last_wasm_update_ms = 0.0f;
        g_state.last_audio_tick_ms = 0.0f;
        g_state.last_composite_ms = 0.0f;
        g_state.last_step_ms = 0.0f;
        return true;
    }

    clear_error();
    start_ms = pd->system->getCurrentTimeMilliseconds();
    g_state.last_wasm_update_ms = 0.0f;
    g_state.last_audio_tick_ms = 0.0f;
    g_state.last_composite_ms = 0.0f;
    phase_start_ms = start_ms;

    update_gamepad_state();
    run_logic = g_state.first_frame
        || ((g_state.logic_tick_counter % (uint32_t)WAMR_PD_LOGIC_TICK_DIVIDER) == 0u);

    if (g_state.first_frame) {
        did_start = true;
        g_state.first_frame = false;
        if (!call_void_export(g_state.fn_start)) {
            phase_end_ms = pd->system->getCurrentTimeMilliseconds();
            g_state.last_wasm_update_ms = (float)(phase_end_ms - phase_start_ms);
            goto done;
        }
    }

    if (run_logic) {
        if (!did_start && !(g_state.w4_mem->systemFlags & W4_SYSTEM_PRESERVE_FRAMEBUFFER)) {
            w4_framebufferClear();
        }

        if (!call_void_export(g_state.fn_update)) {
            phase_end_ms = pd->system->getCurrentTimeMilliseconds();
            g_state.last_wasm_update_ms = (float)(phase_end_ms - phase_start_ms);
            goto done;
        }
        phase_end_ms = pd->system->getCurrentTimeMilliseconds();
        g_state.last_wasm_update_ms = (float)(phase_end_ms - phase_start_ms);
    }
    else {
        phase_end_ms = pd->system->getCurrentTimeMilliseconds();
        g_state.last_wasm_update_ms = 0.0f;
    }

    phase_start_ms = phase_end_ms;
#if !WAMR_PD_DISABLE_AUDIO_TICK
    host_audio_tick();
#endif
    phase_end_ms = pd->system->getCurrentTimeMilliseconds();
    g_state.last_audio_tick_ms = (float)(phase_end_ms - phase_start_ms);

    phase_start_ms = phase_end_ms;
    composite_to_playdate_framebuffer();
    phase_end_ms = pd->system->getCurrentTimeMilliseconds();
    g_state.last_composite_ms = (float)(phase_end_ms - phase_start_ms);

    g_state.frames_since_disk_write++;
    g_state.logic_tick_counter++;
    (void)flush_disk_if_needed(false);

    ok = true;

done:
    end_ms = pd->system->getCurrentTimeMilliseconds();
    g_state.last_step_ms = (float)(end_ms - start_ms);
    if (pd->display && pd->display->getFPS) {
        g_state.last_fps = pd->display->getFPS();
    }

    return ok;
}

static int
lua_wamr_load(lua_State *L)
{
    const char *path = NULL;
    bool ok;

    (void)L;

    if (pd->lua->getArgCount() >= 1 && !pd->lua->argIsNil(1)) {
        path = pd->lua->getArgString(1);
    }

    ok = load_wasm_cart(path ? path : current_cart_path());

    pd->lua->pushBool(ok ? 1 : 0);
    pd->lua->pushFloat(g_state.last_load_ms);
    if (ok) {
        pd->lua->pushNil();
    }
    else {
        pd->lua->pushString(g_state.last_error);
    }
    return 3;
}

static int
lua_wamr_step(lua_State *L)
{
    bool ok;

    (void)L;

    ok = step_wasm_cart();
    pd->lua->pushBool(ok ? 1 : 0);
    pd->lua->pushFloat(g_state.last_step_ms);
    if (ok) {
        pd->lua->pushNil();
    }
    else {
        pd->lua->pushString(g_state.last_error);
    }
    return 3;
}

static int
lua_wamr_unload(lua_State *L)
{
    (void)L;
    cleanup_loaded_module();
    return 0;
}

static int
lua_wamr_set_dither_mode(lua_State *L)
{
    int mode;

    (void)L;

    if (pd->lua->getArgCount() < 1) {
        pd->lua->pushBool(0);
        pd->lua->pushString("missing mode");
        return 2;
    }

    mode = pd->lua->getArgInt(1);
    if (!set_dither_mode(mode)) {
        pd->lua->pushBool(0);
        pd->lua->pushString("invalid mode (0:none,1:ordered)");
        return 2;
    }

    pd->lua->pushBool(1);
    pd->lua->pushNil();
    return 2;
}

static int
lua_wamr_get_dither_mode(lua_State *L)
{
    (void)L;
    pd->lua->pushInt(g_state.dither_mode);
    return 1;
}

static int
lua_wamr_set_log_level(lua_State *L)
{
    int level;

    (void)L;

    if (pd->lua->getArgCount() < 1) {
        pd->lua->pushBool(0);
        pd->lua->pushString("missing level");
        return 2;
    }

    level = pd->lua->getArgInt(1);
    if (level < W4_LOG_ERROR || level > W4_LOG_DEBUG) {
        pd->lua->pushBool(0);
        pd->lua->pushString("invalid level (0:error,1:info,2:debug)");
        return 2;
    }

    g_state.log_level = level;
    pd->lua->pushBool(1);
    pd->lua->pushNil();
    return 2;
}

static int
lua_wamr_set_refresh_rate(lua_State *L)
{
    int mode;

    (void)L;

    if (pd->lua->getArgCount() < 1) {
        pd->lua->pushBool(0);
        pd->lua->pushString("missing mode");
        return 2;
    }

    mode = pd->lua->getArgInt(1);
    if (!set_refresh_rate_mode(mode)) {
        pd->lua->pushBool(0);
        pd->lua->pushString("invalid mode (0:30fps,1:50fps,2:uncapped)");
        return 2;
    }

    pd->lua->pushBool(1);
    pd->lua->pushFloat(g_state.refresh_rate);
    return 2;
}

static int
lua_wamr_get_fps_raw(lua_State *L)
{
    float fps = g_state.last_fps;
    float refresh = g_state.refresh_rate;

    (void)L;

    if (pd->display && pd->display->getFPS) {
        fps = pd->display->getFPS();
    }
    if (pd->display && pd->display->getRefreshRate) {
        refresh = pd->display->getRefreshRate();
    }

    pd->lua->pushFloat(fps);
    pd->lua->pushFloat(refresh);
    return 2;
}

static int
lua_wamr_rescan_carts(lua_State *L)
{
    char preferred_path[W4_MAX_PATH_LEN];

    (void)L;

    strncpy(preferred_path, current_cart_path(), sizeof(preferred_path) - 1);
    preferred_path[sizeof(preferred_path) - 1] = '\0';
    refresh_cart_list(preferred_path);
    pd->lua->pushInt(g_state.cart_count);
    pd->lua->pushString(current_cart_path());
    return 2;
}

static int
lua_wamr_list_carts(lua_State *L)
{
    char joined[W4_MAX_CARTS * W4_MAX_PATH_LEN];
    size_t len = 0;
    int i;

    (void)L;

    joined[0] = '\0';
    for (i = 0; i < g_state.cart_count; i++) {
        const char *path = g_state.carts[i].path;
        size_t plen = strlen(path);

        if (len + plen + 2 >= sizeof(joined)) {
            break;
        }

        memcpy(joined + len, path, plen);
        len += plen;
        joined[len++] = '\n';
    }

    if (len > 0) {
        joined[len - 1] = '\0';
    }

    pd->lua->pushInt(g_state.cart_count);
    pd->lua->pushInt(g_state.selected_cart);
    pd->lua->pushString(joined);
    return 3;
}

static int
lua_wamr_select_cart(lua_State *L)
{
    int idx;

    (void)L;

    if (pd->lua->getArgCount() < 1) {
        pd->lua->pushBool(0);
        pd->lua->pushString("missing index");
        return 2;
    }

    idx = pd->lua->getArgInt(1);
    if (idx < 0 || idx >= g_state.cart_count) {
        pd->lua->pushBool(0);
        pd->lua->pushString("cart index out of range");
        return 2;
    }

    g_state.selected_cart = idx;

    pd->lua->pushBool(1);
    pd->lua->pushString(current_cart_path());
    return 2;
}

static int
lua_wamr_status_raw(lua_State *L)
{
    (void)L;

    pd->lua->pushBool(g_state.loaded ? 1 : 0);
    pd->lua->pushFloat(g_state.last_load_ms);
    pd->lua->pushFloat(g_state.last_step_ms);

    if (g_state.last_error[0] == '\0') {
        pd->lua->pushNil();
    }
    else {
        pd->lua->pushString(g_state.last_error);
    }

    pd->lua->pushString(current_cart_path());
    return 5;
}

static int
lua_wamr_perf_raw(lua_State *L)
{
    (void)L;

    pd->lua->pushFloat(g_state.last_wasm_update_ms);
    pd->lua->pushFloat(g_state.last_audio_tick_ms);
    pd->lua->pushFloat(g_state.last_composite_ms);
    pd->lua->pushFloat(g_state.last_step_ms);
    pd->lua->pushFloat(g_state.last_load_ms);
    return 5;
}

static int
lua_wamr_runtime_config_raw(lua_State *L)
{
    (void)L;

    pd->lua->pushInt(WAMR_PD_LOGIC_TICK_DIVIDER);
    pd->lua->pushBool(WAMR_PD_DISABLE_AUDIO_TICK);
    pd->lua->pushInt(WAMR_PD_COMPOSITE_MODE);
    pd->lua->pushBool(WAMR_PD_ENABLE_AOT);
    pd->lua->pushString(host_audio_backend_name());
    pd->lua->pushInt(g_state.refresh_rate_mode);
    return 6;
}

void
wamr_bridge_init(PlaydateAPI *playdate)
{
    const char *err;

    pd = playdate;
    memset(&g_state, 0, sizeof(g_state));
    g_state.log_level = W4_LOG_INFO;
    g_state.selected_cart = 0;
    g_state.dither_mode = W4_DITHER_ORDERED;
    g_state.render_mode = W4_RENDER_MODE_QUALITY_240;
    g_state.framebuffer_clear_needed = true;
    g_state.refresh_rate_mode = W4_REFRESH_RATE_30;
    g_state.refresh_rate = 30.0f;
    atomic_store_explicit(&g_state.button_down_mask, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_state.button_pressed_mask, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_state.button_released_mask, 0u, memory_order_relaxed);

    (void)set_refresh_rate_mode(g_state.refresh_rate_mode);
    host_audio_set_paused(false);
    g_state.button_callback_enabled = false;

    refresh_cart_list(current_cart_path());

    if (!pd->lua->addFunction(lua_wamr_load, "wamr_load", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_load failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_step, "wamr_step", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_step failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_unload, "wamr_unload", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_unload failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_status_raw, "wamr_status_raw", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_status_raw failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_perf_raw, "wamr_perf_raw", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_perf_raw failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_runtime_config_raw,
                              "wamr_runtime_config_raw", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_runtime_config_raw failed: %s",
                    err);
    }
    if (!pd->lua->addFunction(lua_wamr_set_dither_mode, "wamr_set_dither_mode", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_set_dither_mode failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_get_dither_mode, "wamr_get_dither_mode", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_get_dither_mode failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_set_log_level, "wamr_set_log_level", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_set_log_level failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_set_refresh_rate, "wamr_set_refresh_rate",
                              &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_set_refresh_rate failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_get_fps_raw, "wamr_get_fps_raw", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_get_fps_raw failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_rescan_carts, "wamr_rescan_carts", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_rescan_carts failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_list_carts, "wamr_list_carts", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_list_carts failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_select_cart, "wamr_select_cart", &err)) {
        log_message(W4_LOG_ERROR, "addFunction wamr_select_cart failed: %s", err);
    }
}

void
wamr_bridge_shutdown(void)
{
    cleanup_loaded_module();
    remove_menu_items();

    if (pd && pd->system && pd->system->setButtonCallback) {
        pd->system->setButtonCallback(NULL, NULL, 0);
    }

    if (g_state.audio_source) {
        pd->sound->removeSource(g_state.audio_source);
        g_state.audio_source = NULL;
    }
    host_audio_shutdown();

    if (g_state.runtime_initialized) {
        wasm_runtime_destroy();
        g_state.runtime_initialized = false;
    }
}

void
wamr_bridge_on_pause(void)
{
    g_state.paused = true;
    host_audio_set_paused(true);
    atomic_store_explicit(&g_state.button_pressed_mask, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_state.button_released_mask, 0u, memory_order_relaxed);
    if (g_state.w4_mem) {
        g_state.w4_mem->gamepads[0] = 0;
        g_state.w4_mem->gamepads[1] = 0;
        g_state.w4_mem->gamepads[2] = 0;
        g_state.w4_mem->gamepads[3] = 0;
    }
    (void)flush_disk_if_needed(true);
}

void
wamr_bridge_on_resume(void)
{
    g_state.paused = false;
    g_state.block_button1_until_release = true;
    host_audio_set_paused(false);
    atomic_store_explicit(&g_state.button_pressed_mask, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_state.button_released_mask, 0u, memory_order_relaxed);
}

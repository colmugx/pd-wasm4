#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pd_api.h"
#include "wasm_export.h"

#include "apu.h"
#include "framebuffer.h"
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

#define W4_VIEWPORT_SIZE LCD_ROWS
#define W4_VIEWPORT_X ((LCD_COLUMNS - W4_VIEWPORT_SIZE) / 2)
#define W4_VIEWPORT_Y 0

#define AUDIO_CHUNK_FRAMES 512

#define W4_DITHER_NONE 0
#define W4_DITHER_ORDERED 1
#define W4_DITHER_MODE_COUNT 2

static const char *g_dither_mode_labels[W4_DITHER_MODE_COUNT] = {
    "None",
    "Ordered",
};

static bool g_scale_map_ready;
static uint16_t g_scale_x0[W4_VIEWPORT_SIZE];
static uint16_t g_scale_x1[W4_VIEWPORT_SIZE];
static uint16_t g_scale_xw[W4_VIEWPORT_SIZE];
static uint16_t g_scale_y0[W4_VIEWPORT_SIZE];
static uint16_t g_scale_y1[W4_VIEWPORT_SIZE];
static uint16_t g_scale_yw[W4_VIEWPORT_SIZE];
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
} CartEntry;

typedef struct WamrState {
    bool runtime_initialized;
    bool loaded;

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
    const char *cart_title_ptrs[W4_MAX_CARTS];
    int cart_count;
    int selected_cart;

    PDMenuItem *menu_select_item;
    PDMenuItem *menu_reload_item;
    PDMenuItem *menu_reset_item;
    PDMenuItem *menu_dither_item;
    PDMenuItem *menu_rescan_item;
    const char *dither_mode_ptrs[W4_DITHER_MODE_COUNT];
    int dither_mode;

    SoundSource *audio_source;

    float last_load_ms;
    float last_step_ms;
    char last_error[W4_MAX_ERROR_LEN];
    bool block_button1_until_release;
} WamrState;

static PlaydateAPI *pd;
static WamrState g_state;

static bool load_wasm_cart(const char *path);
static void cleanup_loaded_module(void);
static void refresh_cart_list(const char *preferred_path);
static bool set_dither_mode(int mode);
static void init_scale_map(void);

static void
set_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_state.last_error, sizeof(g_state.last_error), fmt, args);
    va_end(args);
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
        if (strcmp(g_state.carts[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
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
    FileStat st;
    int idx;

    (void)userdata;

    if (!name || !str_ends_with(name, ".wasm")) {
        return;
    }

    if (snprintf(full_path, sizeof(full_path), "%s/%s", W4_CART_DIR, name)
        >= (int)sizeof(full_path)) {
        return;
    }

    if (pd->file->stat(full_path, &st) != 0 || st.isdir) {
        return;
    }

    idx = g_state.cart_count;
    if (idx >= W4_MAX_CARTS) {
        return;
    }

    strncpy(g_state.carts[idx].path, full_path,
            sizeof(g_state.carts[idx].path) - 1);
    g_state.carts[idx].path[sizeof(g_state.carts[idx].path) - 1] = '\0';

    strncpy(g_state.carts[idx].title, name, sizeof(g_state.carts[idx].title) - 1);
    g_state.carts[idx].title[sizeof(g_state.carts[idx].title) - 1] = '\0';

    g_state.cart_count++;
}

static void
remove_menu_items(void)
{
    if (g_state.menu_select_item) {
        pd->system->removeMenuItem(g_state.menu_select_item);
        g_state.menu_select_item = NULL;
    }
    if (g_state.menu_reload_item) {
        pd->system->removeMenuItem(g_state.menu_reload_item);
        g_state.menu_reload_item = NULL;
    }
    if (g_state.menu_reset_item) {
        pd->system->removeMenuItem(g_state.menu_reset_item);
        g_state.menu_reset_item = NULL;
    }
    if (g_state.menu_dither_item) {
        pd->system->removeMenuItem(g_state.menu_dither_item);
        g_state.menu_dither_item = NULL;
    }
    if (g_state.menu_rescan_item) {
        pd->system->removeMenuItem(g_state.menu_rescan_item);
        g_state.menu_rescan_item = NULL;
    }
}

static void
on_menu_select(void *userdata)
{
    int idx;

    (void)userdata;

    if (!g_state.menu_select_item) {
        return;
    }

    idx = pd->system->getMenuItemValue(g_state.menu_select_item);
    if (idx >= 0 && idx < g_state.cart_count) {
        g_state.selected_cart = idx;
        clear_error();
        if (g_state.loaded) {
            (void)load_wasm_cart(current_cart_path());
        }
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
on_menu_dither(void *userdata)
{
    int mode;

    (void)userdata;

    if (!g_state.menu_dither_item) {
        return;
    }

    mode = pd->system->getMenuItemValue(g_state.menu_dither_item);
    (void)set_dither_mode(mode);
}

static void
on_menu_rescan(void *userdata)
{
    char preferred_path[W4_MAX_PATH_LEN];

    (void)userdata;

    strncpy(preferred_path, current_cart_path(), sizeof(preferred_path) - 1);
    preferred_path[sizeof(preferred_path) - 1] = '\0';
    refresh_cart_list(preferred_path);
}

static void
setup_menu_items(void)
{
    int i;

    remove_menu_items();

    if (g_state.cart_count <= 0) {
        return;
    }

    for (i = 0; i < g_state.cart_count; i++) {
        g_state.cart_title_ptrs[i] = g_state.carts[i].title;
    }

    g_state.menu_select_item = pd->system->addOptionsMenuItem(
        "Cartridge", g_state.cart_title_ptrs, g_state.cart_count,
        on_menu_select, NULL);

    if (g_state.menu_select_item && g_state.selected_cart >= 0
        && g_state.selected_cart < g_state.cart_count) {
        pd->system->setMenuItemValue(g_state.menu_select_item,
                                     g_state.selected_cart);
    }

    g_state.menu_reload_item =
        pd->system->addMenuItem("Back To List", on_menu_reload, NULL);
    g_state.menu_reset_item =
        pd->system->addMenuItem("Reset Game", on_menu_reset, NULL);

    for (i = 0; i < W4_DITHER_MODE_COUNT; i++) {
        g_state.dither_mode_ptrs[i] = g_dither_mode_labels[i];
    }
    g_state.menu_dither_item = pd->system->addOptionsMenuItem(
        "Dither", g_state.dither_mode_ptrs, W4_DITHER_MODE_COUNT,
        on_menu_dither, NULL);
    if (g_state.menu_dither_item) {
        pd->system->setMenuItemValue(g_state.menu_dither_item,
                                     g_state.dither_mode);
    }

    g_state.menu_rescan_item =
        pd->system->addMenuItem("Rescan Cartridges", on_menu_rescan, NULL);
}

static void
refresh_cart_list(const char *preferred_path)
{
    int idx;

    g_state.cart_count = 0;

    if (pd->file->listfiles(W4_CART_DIR, collect_cart_callback, NULL, 0) != 0) {
        pd->system->logToConsole("listfiles %s failed: %s", W4_CART_DIR,
                                 pd->file->geterr());
    }

    if (g_state.cart_count == 0) {
        strncpy(g_state.carts[0].path, W4_DEFAULT_CART_PATH,
                sizeof(g_state.carts[0].path) - 1);
        g_state.carts[0].path[sizeof(g_state.carts[0].path) - 1] = '\0';

        strncpy(g_state.carts[0].title, "main.wasm",
                sizeof(g_state.carts[0].title) - 1);
        g_state.carts[0].title[sizeof(g_state.carts[0].title) - 1] = '\0';
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
init_scale_map(void)
{
    int i;

    if (g_scale_map_ready) {
        return;
    }

    for (i = 0; i < W4_VIEWPORT_SIZE; i++) {
        uint32_t x_fp =
            ((uint32_t)i * (uint32_t)(W4_WIDTH - 1) << 16) / (W4_VIEWPORT_SIZE - 1);
        uint32_t y_fp =
            ((uint32_t)i * (uint32_t)(W4_HEIGHT - 1) << 16) / (W4_VIEWPORT_SIZE - 1);
        uint16_t x0 = (uint16_t)(x_fp >> 16);
        uint16_t y0 = (uint16_t)(y_fp >> 16);

        g_scale_x0[i] = x0;
        g_scale_y0[i] = y0;
        g_scale_x1[i] = (x0 + 1 < W4_WIDTH) ? (uint16_t)(x0 + 1) : x0;
        g_scale_y1[i] = (y0 + 1 < W4_HEIGHT) ? (uint16_t)(y0 + 1) : y0;
        g_scale_xw[i] = (uint16_t)(x_fp & 0xffffu);
        g_scale_yw[i] = (uint16_t)(y_fp & 0xffffu);
    }

    g_scale_map_ready = true;
}

static void
composite_to_playdate_framebuffer(void)
{
    static const uint8_t bayer2[2][2] = {
        { 0, 2 },
        { 3, 1 },
    };

    uint8_t *frame;
    uint8_t luma[4];
    uint8_t sorted_luma[4];
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

    memset(frame, 0, LCD_ROWS * LCD_ROWSIZE);

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

    init_scale_map();

    for (pixel_index = 0; pixel_index < (uint32_t)(W4_WIDTH * W4_HEIGHT);
         pixel_index++) {
        uint8_t quartet = g_state.w4_mem->framebuffer[pixel_index >> 2];
        uint8_t color_index = (quartet >> ((pixel_index & 0x3u) << 1)) & 0x3u;
        g_src_luma[pixel_index] = luma[color_index];
    }

    for (y = 0; y < W4_VIEWPORT_SIZE; y++) {
        uint16_t src_y = g_scale_y0[y];
        if (g_scale_yw[y] >= 32768u && g_scale_y1[y] > src_y) {
            src_y = g_scale_y1[y];
        }
        const uint8_t *src_row = g_src_luma + (uint32_t)src_y * W4_WIDTH;
        uint8_t *row = frame + (W4_VIEWPORT_Y + y) * LCD_ROWSIZE;

        for (x = 0; x < W4_VIEWPORT_SIZE; x++) {
            uint16_t src_x = g_scale_x0[x];
            if (g_scale_xw[x] >= 32768u && g_scale_x1[x] > src_x) {
                src_x = g_scale_x1[x];
            }
            uint8_t sample_luma = src_row[src_x];
            int threshold;
            bool black;
            uint8_t *byte = row + ((W4_VIEWPORT_X + x) >> 3);
            uint8_t mask = (uint8_t)(0x80u >> ((W4_VIEWPORT_X + x) & 7));

            if (g_state.dither_mode == W4_DITHER_NONE) {
                threshold = binary_threshold;
                black = sample_luma < threshold;
            }
            else {
                uint32_t normalized =
                    ((uint32_t)(sample_luma - luma_min) * 255u + (luma_range / 2u))
                    / luma_range;
                int ordered_threshold = (int)bayer2[y & 1][x & 1] * 64 + 32;
                black = (int)normalized < ordered_threshold;
            }
            if (black) {
                *byte |= mask;
            }
            else {
                *byte &= (uint8_t)~mask;
            }
        }
    }

    pd->graphics->markUpdatedRows(W4_VIEWPORT_Y,
                                  W4_VIEWPORT_Y + W4_VIEWPORT_SIZE - 1);
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
    w4_apuInit();

    g_state.first_frame = true;
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

    pd->file->mkdir("save");
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
        return;
    }

    bytes_read = pd->file->read(file, g_state.disk.data, sizeof(g_state.disk.data));
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

    pd->file->mkdir("save");

    if (g_state.disk.size == 0) {
        pd->file->unlink(g_state.disk_path, 0);
        g_state.disk_dirty = false;
        g_state.frames_since_disk_write = 0;
        return true;
    }

    file = pd->file->open(g_state.disk_path, kFileWrite);
    if (!file) {
        set_error("open disk failed for %s: %s", g_state.disk_path,
                  pd->file->geterr());
        return false;
    }

    written = pd->file->write(file, g_state.disk.data, g_state.disk.size);
    pd->file->close(file);

    if (written != g_state.disk.size) {
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
    static int16_t interleaved[AUDIO_CHUNK_FRAMES * 2];
    int offset = 0;

    (void)context;

    if (!left || !right || len <= 0) {
        return 0;
    }

    if (!g_state.loaded) {
        memset(left, 0, sizeof(int16_t) * (size_t)len);
        memset(right, 0, sizeof(int16_t) * (size_t)len);
        return 1;
    }

    while (offset < len) {
        int i;
        int chunk = len - offset;

        if (chunk > AUDIO_CHUNK_FRAMES) {
            chunk = AUDIO_CHUNK_FRAMES;
        }

        w4_apuWriteSamples(interleaved, (unsigned long)chunk);
        for (i = 0; i < chunk; i++) {
            left[offset + i] = interleaved[i * 2];
            right[offset + i] = interleaved[i * 2 + 1];
        }

        offset += chunk;
    }

    return 1;
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

    g_state.loaded = false;
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
    w4_apuTone(frequency, duration, volume, flags);
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
    pd->system->logToConsole("[wasm4] %s", str);
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
    pd->system->logToConsole("[wasm4] %s", buf);
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

    pd->system->logToConsole("[wasm4] traceUtf16(len=%d)", (int)byte_length);
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
    pd->system->logToConsole("[wasm4] %s", out);
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
    if (g_state.menu_dither_item) {
        pd->system->setMenuItemValue(g_state.menu_dither_item, mode);
    }
    return true;
}

static bool
ensure_runtime_initialized(void)
{
    RuntimeInitArgs init_args;

    if (g_state.runtime_initialized) {
        return true;
    }

    memset(&init_args, 0, sizeof(init_args));
    init_args.mem_alloc_type = Alloc_With_System_Allocator;
    init_args.native_module_name = "env";
    init_args.native_symbols = g_native_symbols;
    init_args.n_native_symbols =
        (uint32_t)(sizeof(g_native_symbols) / sizeof(g_native_symbols[0]));

    if (!wasm_runtime_full_init(&init_args)) {
        set_error("wasm_runtime_full_init failed");
        return false;
    }

    g_state.runtime_initialized = true;

    if (!g_state.audio_source) {
        g_state.audio_source = pd->sound->addSource(audio_source_callback, NULL, 1);
        if (!g_state.audio_source) {
            pd->system->logToConsole("failed to create audio source");
        }
    }

    return true;
}

static bool
load_wasm_cart(const char *path)
{
    char error_buf[256] = { 0 };
    uint32_t start_ms;
    uint32_t end_ms;
    uint64_t page_count;
    uint64_t bytes_per_page;
    uint64_t mem_size;
    int idx;

    if (!path || path[0] == '\0') {
        path = current_cart_path();
    }

    cleanup_loaded_module();
    clear_error();

    start_ms = pd->system->getCurrentTimeMilliseconds();

    if (!read_file_into_memory(path, &g_state.wasm_bytes, &g_state.wasm_size)) {
        return false;
    }

    if (!ensure_runtime_initialized()) {
        cleanup_loaded_module();
        return false;
    }

    g_state.module = wasm_runtime_load(g_state.wasm_bytes, g_state.wasm_size,
                                       error_buf, sizeof(error_buf));
    if (!g_state.module) {
        set_error("wasm load failed: %s", error_buf);
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
    load_disk_for_cart(path);

    idx = find_cart_index(path);
    if (idx >= 0) {
        g_state.selected_cart = idx;
        if (g_state.menu_select_item) {
            pd->system->setMenuItemValue(g_state.menu_select_item, g_state.selected_cart);
        }
    }

    g_state.loaded = true;
    g_state.block_button1_until_release = true;

    end_ms = pd->system->getCurrentTimeMilliseconds();
    g_state.last_load_ms = (float)(end_ms - start_ms);
    g_state.last_step_ms = 0.0f;

    clear_error();
    return true;
}

static void
update_gamepad_state(void)
{
    PDButtons current;
    PDButtons pushed;
    PDButtons released;
    uint8_t gamepad = 0;

    if (!g_state.w4_mem) {
        return;
    }

    pd->system->getButtonState(&current, &pushed, &released);
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
    uint32_t start_ms;
    uint32_t end_ms;

    if (!g_state.loaded || !g_state.fn_update) {
        set_error("no wasm loaded");
        return false;
    }

    clear_error();
    start_ms = pd->system->getCurrentTimeMilliseconds();

    update_gamepad_state();

    if (g_state.first_frame) {
        g_state.first_frame = false;
        if (!call_void_export(g_state.fn_start)) {
            return false;
        }
    }
    else if (!(g_state.w4_mem->systemFlags & W4_SYSTEM_PRESERVE_FRAMEBUFFER)) {
        w4_framebufferClear();
    }

    if (!call_void_export(g_state.fn_update)) {
        return false;
    }

    w4_apuTick();
    composite_to_playdate_framebuffer();

    g_state.frames_since_disk_write++;
    (void)flush_disk_if_needed(false);

    end_ms = pd->system->getCurrentTimeMilliseconds();
    g_state.last_step_ms = (float)(end_ms - start_ms);

    return true;
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
    if (g_state.menu_select_item) {
        pd->system->setMenuItemValue(g_state.menu_select_item, idx);
    }

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

void
wamr_bridge_init(PlaydateAPI *playdate)
{
    const char *err;

    pd = playdate;
    memset(&g_state, 0, sizeof(g_state));
    g_state.selected_cart = 0;
    g_state.dither_mode = W4_DITHER_ORDERED;

    refresh_cart_list(current_cart_path());

    if (!pd->lua->addFunction(lua_wamr_load, "wamr_load", &err)) {
        pd->system->logToConsole("addFunction wamr_load failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_step, "wamr_step", &err)) {
        pd->system->logToConsole("addFunction wamr_step failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_unload, "wamr_unload", &err)) {
        pd->system->logToConsole("addFunction wamr_unload failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_status_raw, "wamr_status_raw", &err)) {
        pd->system->logToConsole("addFunction wamr_status_raw failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_set_dither_mode, "wamr_set_dither_mode", &err)) {
        pd->system->logToConsole("addFunction wamr_set_dither_mode failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_get_dither_mode, "wamr_get_dither_mode", &err)) {
        pd->system->logToConsole("addFunction wamr_get_dither_mode failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_rescan_carts, "wamr_rescan_carts", &err)) {
        pd->system->logToConsole("addFunction wamr_rescan_carts failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_list_carts, "wamr_list_carts", &err)) {
        pd->system->logToConsole("addFunction wamr_list_carts failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_select_cart, "wamr_select_cart", &err)) {
        pd->system->logToConsole("addFunction wamr_select_cart failed: %s", err);
    }
}

void
wamr_bridge_shutdown(void)
{
    cleanup_loaded_module();
    remove_menu_items();

    if (g_state.audio_source) {
        pd->sound->removeSource(g_state.audio_source);
        g_state.audio_source = NULL;
    }

    if (g_state.runtime_initialized) {
        wasm_runtime_destroy();
        g_state.runtime_initialized = false;
    }
}

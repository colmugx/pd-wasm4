#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "pd_api.h"

#include "backend/cart_catalog.h"
#include "backend/game_backend.h"
#include "backend/menu_controller.h"
#include "backend/runtime_session.h"
#include "host_audio.h"

#define W4_CART_DIR "cart"

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

#ifndef WAMR_PD_ENABLE_DEBUG_OUTPUT
#define WAMR_PD_ENABLE_DEBUG_OUTPUT 0
#endif

#ifndef WAMR_PD_POOL_SIZE_BYTES
#define WAMR_PD_POOL_SIZE_BYTES (256 * 1024)
#endif

#ifndef WAMR_PD_AUDIO_BACKEND
#define WAMR_PD_AUDIO_BACKEND WAMR_PD_AUDIO_BACKEND_NATIVE
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

#if (WAMR_PD_ENABLE_DEBUG_OUTPUT != 0) && (WAMR_PD_ENABLE_DEBUG_OUTPUT != 1)
#error "WAMR_PD_ENABLE_DEBUG_OUTPUT must be 0 or 1"
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

#define W4_LOG_ERROR 0
#define W4_LOG_INFO 1
#define W4_LOG_DEBUG 2

#define W4_REFRESH_RATE_30 0
#define W4_REFRESH_RATE_50 1
#define W4_REFRESH_RATE_UNCAPPED 2

static const char *g_render_mode_labels[W4_RENDER_MODE_COUNT] = {
    "Fast 160",
    "Quality 240",
};

typedef struct WamrState {
    CartEntry carts[W4_MAX_CARTS];
    int cart_count;
    int selected_cart;
    MenuController menu_controller;
    RuntimeSession session;
    int dither_mode;
    int render_mode;
    int log_level;
    int refresh_rate_mode;
    float refresh_rate;
} WamrState;

typedef struct CartCollectContext {
    bool enable_aot;
} CartCollectContext;

static PlaydateAPI *pd;
static WamrState g_state;

static void
copy_cstr_trunc(char *dst, size_t dst_size, const char *src)
{
    size_t len;

    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void
log_message(int level, const char *fmt, ...)
{
#if (WAMR_PD_ENABLE_DEBUG_OUTPUT == 0)
    (void)level;
    (void)fmt;
#else
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
#endif
}

static void
log_file_error(const char *op, const char *path, const char *message)
{
    log_message(W4_LOG_ERROR, "[file][op=%s][path=%s] %s", op ? op : "?",
                path ? path : "?", message ? message : "unknown");
}

static void
runtime_session_log(int level, const char *message, void *userdata)
{
    (void)userdata;
    log_message(level, "%s", message ? message : "");
}

static const char *
current_cart_path(void)
{
    return cart_catalog_current_path(g_state.carts, g_state.cart_count,
                                     g_state.selected_cart);
}

static int
find_cart_index(const char *path)
{
    return cart_catalog_find_index(g_state.carts, g_state.cart_count, path);
}

static void
collect_cart_callback(const char *name, void *userdata)
{
    CartCollectContext *ctx = (CartCollectContext *)userdata;
    bool enable_aot = ctx ? ctx->enable_aot : false;

    (void)cart_catalog_collect_entry(pd, W4_CART_DIR, name, enable_aot, g_state.carts,
                                     &g_state.cart_count, W4_MAX_CARTS);
}

static void
on_menu_reload(void *userdata)
{
    (void)userdata;
    runtime_session_unload(&g_state.session);
    runtime_session_clear_error(&g_state.session);
}

static void
on_menu_reset(void *userdata)
{
    (void)userdata;
    (void)runtime_session_load(&g_state.session, g_state.carts, g_state.cart_count,
                               &g_state.selected_cart, current_cart_path());
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
    runtime_session_set_framebuffer_clear_needed(&g_state.session);
    menu_controller_sync_render_mode(&g_state.menu_controller, pd, mode);
    return true;
}

static bool
on_menu_set_render_mode(int mode, void *userdata)
{
    (void)userdata;
    return set_render_mode(mode);
}

static void
ensure_cart_directory(void)
{
    FileStat st;

    if (pd->file->stat(W4_CART_DIR, &st) == 0) {
        if (!st.isdir) {
            log_message(W4_LOG_ERROR, "%s exists but is not a directory", W4_CART_DIR);
        }
        return;
    }

    if (pd->file->mkdir(W4_CART_DIR) != 0) {
        const char *err = pd->file->geterr();
        if (pd->file->stat(W4_CART_DIR, &st) != 0 || !st.isdir) {
            log_file_error("mkdir", W4_CART_DIR, err);
        }
    }
}

static void
refresh_cart_list(const char *preferred_path)
{
    int idx;
    CartCollectContext ctx = { .enable_aot = (WAMR_PD_ENABLE_AOT != 0) };

    g_state.cart_count = 0;
    ensure_cart_directory();

    if (pd->file->listfiles(W4_CART_DIR, collect_cart_callback, &ctx, 0) != 0) {
        log_file_error("listfiles", W4_CART_DIR, pd->file->geterr());
    }

    if (g_state.cart_count > 0) {
        cart_catalog_sort(g_state.carts, g_state.cart_count);
    }

    idx = find_cart_index(preferred_path);
    if (idx >= 0) {
        g_state.selected_cart = idx;
    }
    else if (g_state.cart_count == 0) {
        g_state.selected_cart = -1;
    }
    else if (g_state.selected_cart < 0 || g_state.selected_cart >= g_state.cart_count) {
        g_state.selected_cart = 0;
    }

    menu_controller_rebuild(&g_state.menu_controller, pd, g_state.render_mode);
}

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

static const char *
current_error_or_null(void)
{
    return runtime_session_error_or_null(&g_state.session);
}

bool
game_backend_load(const char *path, float *out_load_ms, const char **out_error)
{
    bool ok = runtime_session_load(&g_state.session, g_state.carts, g_state.cart_count,
                                   &g_state.selected_cart,
                                   path ? path : current_cart_path());

    if (out_load_ms) {
        *out_load_ms = runtime_session_last_load_ms(&g_state.session);
    }
    if (out_error) {
        *out_error = ok ? NULL : current_error_or_null();
    }
    return ok;
}

bool
game_backend_step(float *out_step_ms, const char **out_error)
{
    bool ok = runtime_session_step(&g_state.session, g_state.render_mode,
                                   g_state.dither_mode);

    if (out_step_ms) {
        *out_step_ms = runtime_session_last_step_ms(&g_state.session);
    }
    if (out_error) {
        *out_error = ok ? NULL : current_error_or_null();
    }
    return ok;
}

void
game_backend_unload(void)
{
    runtime_session_unload(&g_state.session);
}

bool
game_backend_set_dither_mode(int mode, const char **out_error)
{
    bool ok = set_dither_mode(mode);

    if (out_error) {
        *out_error = ok ? NULL : "invalid mode (0:none,1:ordered)";
    }
    return ok;
}

int
game_backend_get_dither_mode(void)
{
    return g_state.dither_mode;
}

bool
game_backend_set_log_level(int level, const char **out_error)
{
    if (level < W4_LOG_ERROR || level > W4_LOG_DEBUG) {
        if (out_error) {
            *out_error = "invalid level (0:error,1:info,2:debug)";
        }
        return false;
    }

    g_state.log_level = level;
    if (out_error) {
        *out_error = NULL;
    }
    return true;
}

bool
game_backend_set_refresh_rate_mode(int mode, float *out_refresh_rate,
                                   const char **out_error)
{
    bool ok = set_refresh_rate_mode(mode);

    if (out_refresh_rate) {
        *out_refresh_rate = g_state.refresh_rate;
    }
    if (out_error) {
        *out_error = ok ? NULL : "invalid mode (0:30fps,1:50fps,2:uncapped)";
    }
    return ok;
}

void
game_backend_get_fps(float *out_fps, float *out_refresh)
{
    float fps = 0.0f;
    float refresh = g_state.refresh_rate;

    if (pd->display && pd->display->getFPS) {
        fps = pd->display->getFPS();
    }
    if (pd->display && pd->display->getRefreshRate) {
        refresh = pd->display->getRefreshRate();
    }

    if (out_fps) {
        *out_fps = fps;
    }
    if (out_refresh) {
        *out_refresh = refresh;
    }
}

int
game_backend_rescan_carts(const char **out_current_path)
{
    char preferred_path[W4_MAX_PATH_LEN];

    copy_cstr_trunc(preferred_path, sizeof(preferred_path), current_cart_path());
    refresh_cart_list(preferred_path);

    if (out_current_path) {
        *out_current_path = current_cart_path();
    }
    return g_state.cart_count;
}

void
game_backend_list_carts(char *joined, size_t joined_size, int *out_count,
                        int *out_selected_index)
{
    size_t len = 0;
    int i;

    if (joined && joined_size > 0) {
        joined[0] = '\0';
    }

    if (joined && joined_size > 1) {
        for (i = 0; i < g_state.cart_count; i++) {
            const CartEntry *entry = &g_state.carts[i];
            const char *path = entry->path;
            char has_aot = (entry->aot_path[0] != '\0') ? '1' : '0';
            size_t plen = strlen(path);

            if (len + plen + 3 >= joined_size) {
                break;
            }

            memcpy(joined + len, path, plen);
            len += plen;
            joined[len++] = '\t';
            joined[len++] = has_aot;
            joined[len++] = '\n';
        }

        if (len > 0) {
            joined[len - 1] = '\0';
        }
    }

    if (out_count) {
        *out_count = g_state.cart_count;
    }
    if (out_selected_index) {
        *out_selected_index = g_state.selected_cart;
    }
}

bool
game_backend_select_cart(int index, const char **out_path_or_error)
{
    if (index < 0 || index >= g_state.cart_count) {
        if (out_path_or_error) {
            *out_path_or_error = "cart index out of range";
        }
        return false;
    }

    g_state.selected_cart = index;
    if (out_path_or_error) {
        *out_path_or_error = current_cart_path();
    }
    return true;
}

void
game_backend_get_status(bool *out_loaded, float *out_load_ms, float *out_step_ms,
                        const char **out_error, const char **out_current_path)
{
    if (out_loaded) {
        *out_loaded = runtime_session_is_loaded(&g_state.session);
    }
    if (out_load_ms) {
        *out_load_ms = runtime_session_last_load_ms(&g_state.session);
    }
    if (out_step_ms) {
        *out_step_ms = runtime_session_last_step_ms(&g_state.session);
    }
    if (out_error) {
        *out_error = current_error_or_null();
    }
    if (out_current_path) {
        *out_current_path = current_cart_path();
    }
}

void
game_backend_get_perf(float *out_wasm_update_ms, float *out_audio_tick_ms,
                      float *out_composite_ms, float *out_step_ms,
                      float *out_load_ms)
{
    RuntimeSessionPerf perf;

    memset(&perf, 0, sizeof(perf));
    runtime_session_get_perf(&g_state.session, &perf);

    if (out_wasm_update_ms) {
        *out_wasm_update_ms = perf.wasm_update_ms;
    }
    if (out_audio_tick_ms) {
        *out_audio_tick_ms = perf.audio_tick_ms;
    }
    if (out_composite_ms) {
        *out_composite_ms = perf.composite_ms;
    }
    if (out_step_ms) {
        *out_step_ms = perf.step_ms;
    }
    if (out_load_ms) {
        *out_load_ms = perf.load_ms;
    }
}

void
game_backend_get_runtime_config(int *out_logic_divider, int *out_audio_disabled,
                                int *out_composite_mode, int *out_aot_enabled,
                                const char **out_audio_backend,
                                int *out_refresh_mode,
                                bool *out_debug_output_enabled)
{
    if (out_logic_divider) {
        *out_logic_divider = WAMR_PD_LOGIC_TICK_DIVIDER;
    }
    if (out_audio_disabled) {
        *out_audio_disabled = WAMR_PD_DISABLE_AUDIO_TICK;
    }
    if (out_composite_mode) {
        *out_composite_mode = WAMR_PD_COMPOSITE_MODE;
    }
    if (out_aot_enabled) {
        *out_aot_enabled = WAMR_PD_ENABLE_AOT;
    }
    if (out_audio_backend) {
        *out_audio_backend = host_audio_backend_name();
    }
    if (out_refresh_mode) {
        *out_refresh_mode = g_state.refresh_rate_mode;
    }
    if (out_debug_output_enabled) {
        *out_debug_output_enabled = (WAMR_PD_ENABLE_DEBUG_OUTPUT != 0);
    }
}

void
game_backend_init(PlaydateAPI *playdate)
{
    pd = playdate;
    memset(&g_state, 0, sizeof(g_state));

    g_state.log_level = W4_LOG_INFO;
    g_state.selected_cart = -1;
    g_state.dither_mode = W4_DITHER_ORDERED;
    g_state.render_mode = W4_RENDER_MODE_QUALITY_240;
    g_state.refresh_rate_mode = W4_REFRESH_RATE_30;
    g_state.refresh_rate = 30.0f;

    menu_controller_init(&g_state.menu_controller);
    menu_controller_configure(&g_state.menu_controller, g_render_mode_labels,
                              W4_RENDER_MODE_COUNT, on_menu_reload, on_menu_reset,
                              on_menu_set_render_mode, NULL);

    runtime_session_init(&g_state.session, pd, runtime_session_log, NULL);
    (void)set_refresh_rate_mode(g_state.refresh_rate_mode);

    refresh_cart_list(current_cart_path());
}

void
game_backend_shutdown(void)
{
    runtime_session_shutdown(&g_state.session);
    menu_controller_remove(&g_state.menu_controller, pd);
}

void
game_backend_on_pause(void)
{
    runtime_session_on_pause(&g_state.session);
}

void
game_backend_on_resume(void)
{
    runtime_session_on_resume(&g_state.session);
}

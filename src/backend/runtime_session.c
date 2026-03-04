#include "backend/runtime_session.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/host_runtime.h"
#include "backend/input_mapper.h"
#include "backend/persistence.h"
#include "backend/render_composer.h"
#include "framebuffer.h"
#include "host_audio.h"
#include "util.h"

#define W4_CART_DIR "cart"

#define WAMR_STACK_SIZE (64 * 1024)
#define WAMR_HEAP_SIZE (64 * 1024)

#define W4_DISK_FLUSH_INTERVAL_FRAMES 30
#define W4_BUTTON_QUEUE_SIZE 32
#define W4_RENDER_MODE_FAST_160 0
#define W4_DIRTY_ROW_SPAN_LIMIT 112

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
runtime_log_message(RuntimeSession *session, int level, const char *message)
{
    if (!session || !session->log_message) {
        return;
    }

    session->log_message(level, message ? message : "", session->log_userdata);
}

static void
runtime_logf(RuntimeSession *session, int level, const char *fmt, ...)
{
    char buf[512];
    va_list args;

    if (!session || !fmt) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    runtime_log_message(session, level, buf);
}

static void
set_error(RuntimeSession *session, const char *fmt, ...)
{
    va_list args;

    if (!session || !fmt) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(session->last_error, sizeof(session->last_error), fmt, args);
    va_end(args);
    runtime_logf(session, HOST_RUNTIME_LOG_ERROR, "%s", session->last_error);
}

static void
log_file_error(const char *op, const char *path, const char *message,
               void *userdata)
{
    RuntimeSession *session = (RuntimeSession *)userdata;

    runtime_logf(session, HOST_RUNTIME_LOG_ERROR, "[file][op=%s][path=%s] %s",
                 op ? op : "?", path ? path : "?", message ? message : "unknown");
}

static void
clear_error(RuntimeSession *session)
{
    if (!session) {
        return;
    }
    session->last_error[0] = '\0';
}

static uint32_t
current_time_ms(RuntimeSession *session)
{
    if (!session || !session->pd || !session->pd->system
        || !session->pd->system->getCurrentTimeMilliseconds) {
        return 0;
    }
    return session->pd->system->getCurrentTimeMilliseconds();
}

static void
zero_audio_buffers(int16_t *left, int16_t *right, int len)
{
    memset(left, 0, sizeof(int16_t) * (size_t)len);
    memset(right, 0, sizeof(int16_t) * (size_t)len);
}

static void
clear_button_event_masks(RuntimeSession *session)
{
    if (!session) {
        return;
    }
    atomic_store_explicit(&session->button_pressed_mask, 0u, memory_order_relaxed);
    atomic_store_explicit(&session->button_released_mask, 0u, memory_order_relaxed);
}

static void
clear_all_button_masks(RuntimeSession *session)
{
    if (!session) {
        return;
    }

    atomic_store_explicit(&session->button_down_mask, 0u, memory_order_relaxed);
    clear_button_event_masks(session);
}

static void
reset_step_metrics(RuntimeSession *session)
{
    if (!session) {
        return;
    }

    session->last_step_ms = 0.0f;
    session->last_wasm_update_ms = 0.0f;
    session->last_audio_tick_ms = 0.0f;
    session->last_composite_ms = 0.0f;
    session->last_fps = 0.0f;
    session->logic_tick_counter = 0;
}

static void
init_w4_memory_state(RuntimeSession *session)
{
    if (!session || !session->w4_memory || !session->w4_mem) {
        return;
    }

    memset(session->w4_memory, 0, W4_FRAMEBUFFER_OFFSET + W4_FRAMEBUFFER_SIZE);

    w4_write32LE(&session->w4_mem->palette[0], 0xe0f8cf);
    w4_write32LE(&session->w4_mem->palette[1], 0x86c06c);
    w4_write32LE(&session->w4_mem->palette[2], 0x306850);
    w4_write32LE(&session->w4_mem->palette[3], 0x071821);

    session->w4_mem->drawColors[0] = 0x03;
    session->w4_mem->drawColors[1] = 0x12;

    w4_write16LE(&session->w4_mem->mouseX, 0x7fff);
    w4_write16LE(&session->w4_mem->mouseY, 0x7fff);
    session->w4_mem->mouseButtons = 0;

    input_mapper_clear_gamepads(session->w4_mem);
    session->w4_mem->systemFlags = 0;

    w4_framebufferInit(session->w4_mem->drawColors, session->w4_mem->framebuffer);
    host_audio_reset();

    session->first_frame = true;
    session->framebuffer_clear_needed = true;
    session->prev_w4_framebuffer_valid = false;
}

static void
snapshot_w4_framebuffer(RuntimeSession *session)
{
    if (!session || !session->w4_mem) {
        return;
    }

    memcpy(session->prev_w4_framebuffer, session->w4_mem->framebuffer,
           W4_FRAMEBUFFER_SIZE);
    session->prev_w4_framebuffer_valid = true;
}

static bool
detect_dirty_row_span(const RuntimeSession *session, int *out_min_row,
                      int *out_max_row)
{
    int y;
    int min_row = -1;
    int max_row = -1;
    const uint8_t *curr;
    const uint8_t *prev;

    if (!session || !session->w4_mem || !session->prev_w4_framebuffer_valid
        || !out_min_row || !out_max_row) {
        return false;
    }

    curr = session->w4_mem->framebuffer;
    prev = session->prev_w4_framebuffer;

    for (y = 0; y < W4_HEIGHT; y++) {
        size_t row_off = (size_t)y * (W4_WIDTH / 4);
        if (memcmp(curr + row_off, prev + row_off, (size_t)(W4_WIDTH / 4)) != 0) {
            if (min_row < 0) {
                min_row = y;
            }
            max_row = y;
        }
    }

    if (min_row < 0 || max_row < min_row) {
        return false;
    }

    *out_min_row = min_row;
    *out_max_row = max_row;
    return true;
}

static void
load_disk_for_cart(RuntimeSession *session, const char *cart_path)
{
    if (!session || !session->pd || !cart_path || cart_path[0] == '\0') {
        return;
    }

    persistence_load_for_cart(session->pd, cart_path, &session->disk,
                              &session->disk_dirty,
                              &session->frames_since_disk_write,
                              session->disk_path, sizeof(session->disk_path),
                              log_file_error, session);
}

static bool
flush_disk_if_needed(RuntimeSession *session, bool force)
{
    bool ok;
    char err[RUNTIME_SESSION_MAX_ERROR_LEN];

    if (!session) {
        return false;
    }

    err[0] = '\0';
    ok = persistence_flush_if_needed(session->pd, &session->disk, &session->disk_dirty,
                                     &session->frames_since_disk_write,
                                     session->disk_path, force,
                                     W4_DISK_FLUSH_INTERVAL_FRAMES,
                                     log_file_error, session, err, sizeof(err));
    if (!ok && err[0] != '\0') {
        set_error(session, "%s", err);
    }
    return ok;
}

static bool
read_file_into_memory(RuntimeSession *session, const char *path, uint8_t **out_buf,
                      uint32_t *out_size)
{
    FileStat st;
    SDFile *file;
    uint8_t *buf;
    uint32_t total_read = 0;
    int read_count;

    if (!session || !session->pd || !session->pd->file || !path || !out_buf
        || !out_size) {
        set_error(session, "invalid read_file arguments");
        return false;
    }

    if (session->pd->file->stat(path, &st) != 0) {
        log_file_error("stat", path, session->pd->file->geterr(), session);
        set_error(session, "stat failed for %s: %s", path,
                  session->pd->file->geterr());
        return false;
    }

    if (st.isdir) {
        set_error(session, "%s is a directory", path);
        return false;
    }

    if (st.size == 0) {
        set_error(session, "%s is empty", path);
        return false;
    }

    file = session->pd->file->open(path, kFileReadData);
    if (!file) {
        log_file_error("open", path, session->pd->file->geterr(), session);
        set_error(session, "open failed for %s in data/%s: %s", path, W4_CART_DIR,
                  session->pd->file->geterr());
        return false;
    }

    buf = malloc(st.size);
    if (!buf) {
        session->pd->file->close(file);
        set_error(session, "malloc failed for %u bytes", st.size);
        return false;
    }

    while (total_read < st.size) {
        read_count =
            session->pd->file->read(file, buf + total_read, st.size - total_read);
        if (read_count <= 0) {
            break;
        }
        total_read += (uint32_t)read_count;
    }

    session->pd->file->close(file);

    if (total_read != st.size) {
        log_file_error("read", path, session->pd->file->geterr(), session);
        free(buf);
        set_error(session, "read failed for %s (%u/%u)", path, total_read, st.size);
        return false;
    }

    *out_buf = buf;
    *out_size = st.size;
    return true;
}

static int
on_button_event_callback(PDButtons button, int down, uint32_t when, void *userdata)
{
    RuntimeSession *session = (RuntimeSession *)userdata;

    (void)when;
    runtime_session_on_button_event(session, button, down);
    return 0;
}

static int
audio_source_callback(void *context, int16_t *left, int16_t *right, int len)
{
#if WAMR_PD_DISABLE_AUDIO_TICK
    (void)context;

    if (!left || !right || len <= 0) {
        return 0;
    }

    zero_audio_buffers(left, right, len);
    return 1;
#else
    RuntimeSession *session = (RuntimeSession *)context;
    bool output_enabled;

    if (!left || !right || len <= 0) {
        return 0;
    }

    output_enabled = session && session->loaded && !session->paused;
    return host_audio_render(left, right, len, output_enabled);
#endif
}

static bool
call_void_export(RuntimeSession *session, wasm_function_inst_t fn)
{
    if (!session) {
        return false;
    }
    if (!fn) {
        return true;
    }

    if (!wasm_runtime_call_wasm(session->exec_env, fn, 0, NULL)) {
        const char *exception = wasm_runtime_get_exception(session->module_inst);
        set_error(session, "wasm exception: %s", exception ? exception : "unknown");
        return false;
    }

    return true;
}

static void
cleanup_loaded_module(RuntimeSession *session)
{
    if (!session) {
        return;
    }

    (void)flush_disk_if_needed(session, true);

    if (session->exec_env) {
        wasm_runtime_destroy_exec_env(session->exec_env);
        session->exec_env = NULL;
    }

    if (session->module_inst) {
        wasm_runtime_deinstantiate(session->module_inst);
        session->module_inst = NULL;
    }

    if (session->module) {
        wasm_runtime_unload(session->module);
        session->module = NULL;
    }

    if (session->wasm_bytes) {
        free(session->wasm_bytes);
        session->wasm_bytes = NULL;
    }

    session->wasm_size = 0;
    session->fn_start = NULL;
    session->fn_update = NULL;
    session->memory_inst = NULL;
    session->w4_memory = NULL;
    session->w4_memory_size = 0;
    session->w4_mem = NULL;
    session->first_frame = false;
    session->framebuffer_clear_needed = true;
    session->prev_w4_framebuffer_valid = false;

    session->loaded = false;
    session->paused = false;
    host_audio_set_paused(false);
    host_audio_reset();
    if (session->button_callback_enabled && session->pd && session->pd->system
        && session->pd->system->setButtonCallback) {
        session->pd->system->setButtonCallback(NULL, NULL, 0);
    }
    session->button_callback_enabled = false;
    reset_step_metrics(session);
    clear_all_button_masks(session);
}

static void
host_runtime_set_error(const char *message, void *userdata)
{
    RuntimeSession *session = (RuntimeSession *)userdata;

    set_error(session, "%s", message ? message : "unknown host error");
}

static void
host_runtime_log(int level, const char *message, void *userdata)
{
    RuntimeSession *session = (RuntimeSession *)userdata;

    runtime_log_message(session, level, message ? message : "");
}

static bool
ensure_runtime_initialized(RuntimeSession *session)
{
    RuntimeInitArgs init_args;
    NativeSymbol *native_symbols;
    uint32_t native_symbol_count = 0;

    if (!session) {
        return false;
    }
    if (session->runtime_initialized) {
        return true;
    }

    memset(&init_args, 0, sizeof(init_args));
#if WAMR_PD_USE_POOL_ALLOC
    memset(session->wamr_pool, 0, sizeof(session->wamr_pool));
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = session->wamr_pool;
    init_args.mem_alloc_option.pool.heap_size =
        (unsigned int)sizeof(session->wamr_pool);
#else
    init_args.mem_alloc_type = Alloc_With_System_Allocator;
#endif

    native_symbols = host_runtime_get_native_symbols(&native_symbol_count);
    init_args.native_module_name = "env";
    init_args.native_symbols = native_symbols;
    init_args.n_native_symbols = native_symbol_count;

    if (!wasm_runtime_full_init(&init_args)) {
        set_error(session, "wasm_runtime_full_init failed");
        return false;
    }

    session->runtime_initialized = true;
    host_audio_init();

    if (!session->audio_source && session->pd && session->pd->sound) {
        session->audio_source =
            session->pd->sound->addSource(audio_source_callback, session, 1);
        if (!session->audio_source) {
            runtime_logf(session, HOST_RUNTIME_LOG_ERROR,
                         "failed to create audio source");
        }
    }

    return true;
}

static bool
load_module_from_path(RuntimeSession *session, const char *module_path,
                      const char *cart_path)
{
    char error_buf[256] = { 0 };
    uint64_t page_count;
    uint64_t bytes_per_page;
    uint64_t mem_size;

    if (!session) {
        return false;
    }
    if (!module_path || module_path[0] == '\0') {
        set_error(session, "invalid module path");
        return false;
    }

    if (!read_file_into_memory(session, module_path, &session->wasm_bytes,
                               &session->wasm_size)) {
        return false;
    }

    if (!ensure_runtime_initialized(session)) {
        cleanup_loaded_module(session);
        return false;
    }

    session->module = wasm_runtime_load(session->wasm_bytes, session->wasm_size,
                                        error_buf, sizeof(error_buf));
    if (!session->module) {
        set_error(session, "module load failed for %s: %s", module_path, error_buf);
        cleanup_loaded_module(session);
        return false;
    }

    session->module_inst =
        wasm_runtime_instantiate(session->module, WAMR_STACK_SIZE, WAMR_HEAP_SIZE,
                                 error_buf, sizeof(error_buf));
    if (!session->module_inst) {
        set_error(session, "wasm instantiate failed: %s", error_buf);
        cleanup_loaded_module(session);
        return false;
    }

    session->exec_env =
        wasm_runtime_create_exec_env(session->module_inst, WAMR_STACK_SIZE);
    if (!session->exec_env) {
        set_error(session, "wasm create exec env failed");
        cleanup_loaded_module(session);
        return false;
    }

    session->fn_start = wasm_runtime_lookup_function(session->module_inst, "start");
    session->fn_update = wasm_runtime_lookup_function(session->module_inst, "update");

    if (!session->fn_update) {
        set_error(session, "missing export: update");
        cleanup_loaded_module(session);
        return false;
    }

    session->memory_inst = wasm_runtime_get_default_memory(session->module_inst);
    if (!session->memory_inst) {
        set_error(session, "missing default memory");
        cleanup_loaded_module(session);
        return false;
    }

    session->w4_memory = (uint8_t *)wasm_memory_get_base_address(session->memory_inst);
    if (!session->w4_memory) {
        set_error(session, "failed to get memory base");
        cleanup_loaded_module(session);
        return false;
    }

    page_count = wasm_memory_get_cur_page_count(session->memory_inst);
    bytes_per_page = wasm_memory_get_bytes_per_page(session->memory_inst);
    mem_size = page_count * bytes_per_page;

    if (mem_size < W4_MEMORY_SIZE) {
        set_error(session, "linear memory too small: %u", (uint32_t)mem_size);
        cleanup_loaded_module(session);
        return false;
    }

    session->w4_memory_size = (uint32_t)mem_size;
    session->w4_mem = (W4MemoryMap *)session->w4_memory;

    init_w4_memory_state(session);
    load_disk_for_cart(session, cart_path ? cart_path : module_path);

    session->loaded = true;
    session->paused = false;
    host_audio_set_paused(false);
    if (session->pd && session->pd->system
        && session->pd->system->setButtonCallback) {
        session->pd->system->setButtonCallback(on_button_event_callback, session,
                                               W4_BUTTON_QUEUE_SIZE);
        session->button_callback_enabled = true;
    }
    session->block_button1_until_release = true;
    reset_step_metrics(session);
    clear_button_event_masks(session);

    clear_error(session);
    return true;
}

static const char *
selected_cart_path(const CartEntry *carts, int cart_count, int selected_cart)
{
    return cart_catalog_current_path(carts, cart_count, selected_cart);
}

static void
update_gamepad_state(RuntimeSession *session)
{
    if (!session) {
        return;
    }

    input_mapper_update_gamepad(session->pd, session->button_callback_enabled,
                                &session->button_down_mask,
                                &session->button_pressed_mask,
                                &session->button_released_mask,
                                &session->block_button1_until_release,
                                session->w4_mem);
}

void
runtime_session_init(RuntimeSession *session, PlaydateAPI *pd,
                     RuntimeSessionLogFn log_message, void *log_userdata)
{
    HostRuntimeContext host_ctx;

    if (!session) {
        return;
    }

    memset(session, 0, sizeof(*session));
    session->pd = pd;
    session->log_message = log_message;
    session->log_userdata = log_userdata;
    session->framebuffer_clear_needed = true;
    clear_all_button_masks(session);

    memset(&host_ctx, 0, sizeof(host_ctx));
    host_ctx.w4_memory = &session->w4_memory;
    host_ctx.w4_memory_size = &session->w4_memory_size;
    host_ctx.disk = &session->disk;
    host_ctx.disk_dirty = &session->disk_dirty;
    host_ctx.frames_since_disk_write = &session->frames_since_disk_write;
    host_ctx.set_error = host_runtime_set_error;
    host_ctx.log_message = host_runtime_log;
    host_ctx.userdata = session;
    host_runtime_bind_context(&host_ctx);

    host_audio_set_paused(false);
}

void
runtime_session_shutdown(RuntimeSession *session)
{
    if (!session) {
        return;
    }

    cleanup_loaded_module(session);

    if (session->pd && session->pd->system && session->pd->system->setButtonCallback) {
        session->pd->system->setButtonCallback(NULL, NULL, 0);
    }

    if (session->audio_source && session->pd && session->pd->sound) {
        session->pd->sound->removeSource(session->audio_source);
        session->audio_source = NULL;
    }
    host_audio_shutdown();

    if (session->runtime_initialized) {
        wasm_runtime_destroy();
        session->runtime_initialized = false;
    }

    host_runtime_bind_context(NULL);
}

bool
runtime_session_load(RuntimeSession *session, const CartEntry *carts, int cart_count,
                     int *inout_selected_cart, const char *path)
{
    const char *primary_path = NULL;
    const char *fallback_path = NULL;
    const char *cart_path = NULL;
    char primary_error[RUNTIME_SESSION_MAX_ERROR_LEN];
    uint32_t start_ms;
    uint32_t end_ms;
    int idx;
    int selected = -1;

    if (!session) {
        return false;
    }
    if (inout_selected_cart) {
        selected = *inout_selected_cart;
    }

    if (!path || path[0] == '\0') {
        if (cart_count == 0) {
            set_error(session, "no carts found in data/%s", W4_CART_DIR);
            return false;
        }
        path = selected_cart_path(carts, cart_count, selected);
    }

    idx = cart_catalog_find_index(carts, cart_count, path);
    if (idx >= 0) {
        const CartEntry *entry = &carts[idx];

        selected = idx;
        if (inout_selected_cart) {
            *inout_selected_cart = idx;
        }
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
    if (!primary_path || primary_path[0] == '\0') {
        set_error(session, "no cart path selected");
        return false;
    }

    cleanup_loaded_module(session);
    clear_error(session);
    primary_error[0] = '\0';
    start_ms = current_time_ms(session);

    if (load_module_from_path(session, primary_path, cart_path)) {
        goto done;
    }

    copy_cstr_trunc(primary_error, sizeof(primary_error), session->last_error);

    if (fallback_path && strcmp(fallback_path, primary_path) != 0) {
        cleanup_loaded_module(session);
        clear_error(session);
        if (load_module_from_path(session, fallback_path, cart_path)) {
            runtime_logf(session, HOST_RUNTIME_LOG_INFO,
                         "AOT load failed for %s, fallback to wasm: %s",
                         primary_path, primary_error);
            goto done;
        }
        set_error(session, "primary failed: %s; fallback failed: %s", primary_error,
                  session->last_error);
        cleanup_loaded_module(session);
        return false;
    }

    set_error(session, "%s", primary_error);
    cleanup_loaded_module(session);
    return false;

done:
    if (inout_selected_cart) {
        *inout_selected_cart = selected;
    }
    end_ms = current_time_ms(session);
    session->last_load_ms = (float)(end_ms - start_ms);
    return true;
}

bool
runtime_session_step(RuntimeSession *session, int render_mode, int dither_mode)
{
    bool ok = false;
    bool run_logic;
    bool did_start = false;
    bool skip_composite = false;
    bool dirty_rows_valid = false;
    int dirty_row_min = 0;
    int dirty_row_max = 0;
    uint32_t start_ms;
    uint32_t phase_start_ms;
    uint32_t phase_end_ms;
    uint32_t end_ms;

    if (!session || !session->pd) {
        return false;
    }
    if (!session->loaded || !session->fn_update) {
        set_error(session, "no wasm loaded");
        return false;
    }
    if (session->paused) {
        session->last_wasm_update_ms = 0.0f;
        session->last_audio_tick_ms = 0.0f;
        session->last_composite_ms = 0.0f;
        session->last_step_ms = 0.0f;
        return true;
    }

    clear_error(session);
    start_ms = current_time_ms(session);
    session->last_wasm_update_ms = 0.0f;
    session->last_audio_tick_ms = 0.0f;
    session->last_composite_ms = 0.0f;
    phase_start_ms = start_ms;

    update_gamepad_state(session);
    run_logic = session->first_frame
        || ((session->logic_tick_counter % (uint32_t)WAMR_PD_LOGIC_TICK_DIVIDER)
            == 0u);

    if (session->first_frame) {
        did_start = true;
        session->first_frame = false;
        if (!call_void_export(session, session->fn_start)) {
            phase_end_ms = current_time_ms(session);
            session->last_wasm_update_ms = (float)(phase_end_ms - phase_start_ms);
            goto done;
        }
    }

    if (run_logic) {
        if (!did_start
            && !(session->w4_mem->systemFlags & W4_SYSTEM_PRESERVE_FRAMEBUFFER)) {
            w4_framebufferClear();
        }

        if (!call_void_export(session, session->fn_update)) {
            phase_end_ms = current_time_ms(session);
            session->last_wasm_update_ms = (float)(phase_end_ms - phase_start_ms);
            goto done;
        }
        phase_end_ms = current_time_ms(session);
        session->last_wasm_update_ms = (float)(phase_end_ms - phase_start_ms);
    }
    else {
        phase_end_ms = current_time_ms(session);
        session->last_wasm_update_ms = 0.0f;
    }

    phase_start_ms = phase_end_ms;
    if (!session->framebuffer_clear_needed && session->prev_w4_framebuffer_valid) {
        if (!run_logic) {
            skip_composite = true;
        }
        else {
            bool has_dirty =
                detect_dirty_row_span(session, &dirty_row_min, &dirty_row_max);
            if (!has_dirty) {
                skip_composite = true;
            }
            else if (render_mode != W4_RENDER_MODE_FAST_160) {
                int span = dirty_row_max - dirty_row_min + 1;
                if (span <= W4_DIRTY_ROW_SPAN_LIMIT) {
                    dirty_rows_valid = true;
                }
            }
        }
    }

#if !WAMR_PD_DISABLE_AUDIO_TICK
    host_audio_tick();
#endif
    phase_end_ms = current_time_ms(session);
    session->last_audio_tick_ms = (float)(phase_end_ms - phase_start_ms);

    phase_start_ms = phase_end_ms;
    if (!skip_composite) {
        render_composer_composite(session->pd, session->w4_mem, render_mode,
                                  dither_mode, &session->framebuffer_clear_needed,
                                  dirty_rows_valid, dirty_row_min, dirty_row_max);
    }
    phase_end_ms = current_time_ms(session);
    session->last_composite_ms = (float)(phase_end_ms - phase_start_ms);

    if (run_logic || !session->prev_w4_framebuffer_valid) {
        snapshot_w4_framebuffer(session);
    }

    session->frames_since_disk_write++;
    session->logic_tick_counter++;
    (void)flush_disk_if_needed(session, false);

    ok = true;

done:
    end_ms = current_time_ms(session);
    session->last_step_ms = (float)(end_ms - start_ms);
    if (session->pd && session->pd->display && session->pd->display->getFPS) {
        session->last_fps = session->pd->display->getFPS();
    }

    return ok;
}

void
runtime_session_unload(RuntimeSession *session)
{
    cleanup_loaded_module(session);
}

void
runtime_session_clear_error(RuntimeSession *session)
{
    clear_error(session);
}

const char *
runtime_session_error_or_null(const RuntimeSession *session)
{
    if (!session) {
        return "runtime session unavailable";
    }
    return (session->last_error[0] != '\0') ? session->last_error : NULL;
}

bool
runtime_session_is_loaded(const RuntimeSession *session)
{
    return session ? session->loaded : false;
}

float
runtime_session_last_load_ms(const RuntimeSession *session)
{
    return session ? session->last_load_ms : 0.0f;
}

float
runtime_session_last_step_ms(const RuntimeSession *session)
{
    return session ? session->last_step_ms : 0.0f;
}

void
runtime_session_get_perf(const RuntimeSession *session, RuntimeSessionPerf *out_perf)
{
    if (!session || !out_perf) {
        return;
    }

    out_perf->wasm_update_ms = session->last_wasm_update_ms;
    out_perf->audio_tick_ms = session->last_audio_tick_ms;
    out_perf->composite_ms = session->last_composite_ms;
    out_perf->step_ms = session->last_step_ms;
    out_perf->load_ms = session->last_load_ms;
}

void
runtime_session_set_framebuffer_clear_needed(RuntimeSession *session)
{
    if (!session) {
        return;
    }
    session->framebuffer_clear_needed = true;
}

void
runtime_session_on_button_event(RuntimeSession *session, PDButtons button, int down)
{
    uint32_t bit = (uint32_t)button;

    if (!session) {
        return;
    }

    if (down) {
        (void)atomic_fetch_or_explicit(&session->button_down_mask, bit,
                                       memory_order_relaxed);
        (void)atomic_fetch_or_explicit(&session->button_pressed_mask, bit,
                                       memory_order_relaxed);
    }
    else {
        (void)atomic_fetch_and_explicit(&session->button_down_mask, ~bit,
                                        memory_order_relaxed);
        (void)atomic_fetch_or_explicit(&session->button_released_mask, bit,
                                       memory_order_relaxed);
    }
}

void
runtime_session_on_pause(RuntimeSession *session)
{
    if (!session) {
        return;
    }

    session->paused = true;
    host_audio_set_paused(true);
    clear_button_event_masks(session);
    input_mapper_clear_gamepads(session->w4_mem);
    (void)flush_disk_if_needed(session, true);
}

void
runtime_session_on_resume(RuntimeSession *session)
{
    if (!session) {
        return;
    }

    session->paused = false;
    session->block_button1_until_release = true;
    host_audio_set_paused(false);
    clear_button_event_masks(session);
}

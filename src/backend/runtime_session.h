#ifndef PLAYDATE_WAMR_RUNTIME_SESSION_H
#define PLAYDATE_WAMR_RUNTIME_SESSION_H

#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>

#include "pd_api.h"
#include "wasm_export.h"

#include "backend/cart_catalog.h"
#include "backend/persistence.h"
#include "deps/wasm4/w4_memory_map.h"

#define RUNTIME_SESSION_MAX_ERROR_LEN 256

typedef void (*RuntimeSessionLogFn)(int level, const char *message,
                                    void *userdata);

typedef struct RuntimeSession {
    PlaydateAPI *pd;
    RuntimeSessionLogFn log_message;
    void *log_userdata;
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
    SoundSource *audio_source;
    float last_load_ms;
    float last_step_ms;
    float last_wasm_update_ms;
    float last_audio_tick_ms;
    float last_composite_ms;
    float last_fps;
    char last_error[RUNTIME_SESSION_MAX_ERROR_LEN];
    bool block_button1_until_release;
    bool framebuffer_clear_needed;
    bool paused;
    bool button_callback_enabled;
    atomic_uint_fast32_t button_down_mask;
    atomic_uint_fast32_t button_pressed_mask;
    atomic_uint_fast32_t button_released_mask;
    uint32_t logic_tick_counter;
} RuntimeSession;

typedef struct RuntimeSessionPerf {
    float wasm_update_ms;
    float audio_tick_ms;
    float composite_ms;
    float step_ms;
    float load_ms;
} RuntimeSessionPerf;

void
runtime_session_init(RuntimeSession *session, PlaydateAPI *pd,
                     RuntimeSessionLogFn log_message, void *log_userdata);

void
runtime_session_shutdown(RuntimeSession *session);

bool
runtime_session_load(RuntimeSession *session, const CartEntry *carts,
                     int cart_count, int *inout_selected_cart, const char *path);

bool
runtime_session_step(RuntimeSession *session, int render_mode, int dither_mode);

void
runtime_session_unload(RuntimeSession *session);

void
runtime_session_clear_error(RuntimeSession *session);

const char *
runtime_session_error_or_null(const RuntimeSession *session);

bool
runtime_session_is_loaded(const RuntimeSession *session);

float
runtime_session_last_load_ms(const RuntimeSession *session);

float
runtime_session_last_step_ms(const RuntimeSession *session);

void
runtime_session_get_perf(const RuntimeSession *session, RuntimeSessionPerf *out_perf);

void
runtime_session_set_framebuffer_clear_needed(RuntimeSession *session);

void
runtime_session_on_button_event(RuntimeSession *session, PDButtons button, int down);

void
runtime_session_on_pause(RuntimeSession *session);

void
runtime_session_on_resume(RuntimeSession *session);

#endif

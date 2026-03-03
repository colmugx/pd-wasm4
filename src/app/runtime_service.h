#ifndef PLAYDATE_WASM4_APP_RUNTIME_SERVICE_H
#define PLAYDATE_WASM4_APP_RUNTIME_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#define APP_RUNTIME_CART_JOINED_CAPACITY (64 * 128)

typedef struct AppRuntimeStatus {
    bool loaded;
    float load_ms;
    float step_ms;
    const char *error;
    const char *current_path;
} AppRuntimeStatus;

typedef struct AppRuntimePerf {
    float wasm_update_ms;
    float audio_tick_ms;
    float composite_ms;
    float step_ms;
    float load_ms;
} AppRuntimePerf;

typedef struct AppRuntimeConfig {
    int logic_divider;
    int audio_disabled;
    int composite_mode;
    int aot_enabled;
    const char *audio_backend;
    int refresh_rate_mode;
} AppRuntimeConfig;

bool
app_runtime_service_load(const char *path, float *out_load_ms, const char **out_error);

bool
app_runtime_service_step(float *out_step_ms, const char **out_error);

void
app_runtime_service_unload(void);

bool
app_runtime_service_set_dither_mode(int mode, const char **out_error);

int
app_runtime_service_get_dither_mode(void);

bool
app_runtime_service_set_log_level(int level, const char **out_error);

bool
app_runtime_service_set_refresh_rate_mode(int mode, float *out_refresh_rate,
                                          const char **out_error);

void
app_runtime_service_get_fps(float *out_fps, float *out_refresh);

int
app_runtime_service_rescan_carts(const char **out_current_path);

void
app_runtime_service_list_carts(char *joined, size_t joined_size, int *out_count,
                               int *out_selected_index);

bool
app_runtime_service_select_cart(int index, const char **out_path_or_error);

void
app_runtime_service_get_status(AppRuntimeStatus *out_status);

void
app_runtime_service_get_perf(AppRuntimePerf *out_perf);

void
app_runtime_service_get_runtime_config(AppRuntimeConfig *out_config);

#endif

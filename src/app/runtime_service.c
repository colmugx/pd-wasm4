#include "app/runtime_service.h"

#include <string.h>

#include "backend/game_backend.h"

static const char *
normalize_error(bool ok, const char *error)
{
    if (ok) {
        return NULL;
    }
    if (error && error[0] != '\0') {
        return error;
    }
    return "runtime operation failed";
}

bool
app_runtime_service_load(const char *path, float *out_load_ms, const char **out_error)
{
    bool ok;
    const char *error = NULL;

    ok = game_backend_load(path, out_load_ms, &error);
    if (out_error) {
        *out_error = normalize_error(ok, error);
    }
    return ok;
}

bool
app_runtime_service_step(float *out_step_ms, const char **out_error)
{
    bool ok;
    const char *error = NULL;

    ok = game_backend_step(out_step_ms, &error);
    if (out_error) {
        *out_error = normalize_error(ok, error);
    }
    return ok;
}

void
app_runtime_service_unload(void)
{
    game_backend_unload();
}

bool
app_runtime_service_set_dither_mode(int mode, const char **out_error)
{
    bool ok;
    const char *error = NULL;

    ok = game_backend_set_dither_mode(mode, &error);
    if (out_error) {
        *out_error = normalize_error(ok, error);
    }
    return ok;
}

int
app_runtime_service_get_dither_mode(void)
{
    return game_backend_get_dither_mode();
}

bool
app_runtime_service_set_log_level(int level, const char **out_error)
{
    bool ok;
    const char *error = NULL;

    ok = game_backend_set_log_level(level, &error);
    if (out_error) {
        *out_error = normalize_error(ok, error);
    }
    return ok;
}

bool
app_runtime_service_set_refresh_rate_mode(int mode, float *out_refresh_rate,
                                          const char **out_error)
{
    bool ok;
    const char *error = NULL;

    ok = game_backend_set_refresh_rate_mode(mode, out_refresh_rate, &error);
    if (out_error) {
        *out_error = normalize_error(ok, error);
    }
    return ok;
}

void
app_runtime_service_get_fps(float *out_fps, float *out_refresh)
{
    game_backend_get_fps(out_fps, out_refresh);
}

int
app_runtime_service_rescan_carts(const char **out_current_path)
{
    return game_backend_rescan_carts(out_current_path);
}

void
app_runtime_service_list_carts(char *joined, size_t joined_size, int *out_count,
                               int *out_selected_index)
{
    if (joined && joined_size > 0) {
        joined[0] = '\0';
    }

    game_backend_list_carts(joined, joined_size, out_count, out_selected_index);
}

bool
app_runtime_service_select_cart(int index, const char **out_path_or_error)
{
    bool ok;
    const char *path_or_error = NULL;

    ok = game_backend_select_cart(index, &path_or_error);
    if (out_path_or_error) {
        if (ok) {
            *out_path_or_error = (path_or_error && path_or_error[0] != '\0')
                ? path_or_error
                : "";
        }
        else {
            *out_path_or_error = normalize_error(false, path_or_error);
        }
    }

    return ok;
}

void
app_runtime_service_get_status(AppRuntimeStatus *out_status)
{
    AppRuntimeStatus status;

    if (!out_status) {
        return;
    }

    memset(&status, 0, sizeof(status));
    game_backend_get_status(&status.loaded, &status.load_ms, &status.step_ms,
                            &status.error, &status.current_path);
    status.error = (status.error && status.error[0] != '\0') ? status.error : NULL;
    if (!status.current_path) {
        status.current_path = "";
    }

    *out_status = status;
}

void
app_runtime_service_get_perf(AppRuntimePerf *out_perf)
{
    if (!out_perf) {
        return;
    }

    memset(out_perf, 0, sizeof(*out_perf));
    game_backend_get_perf(&out_perf->wasm_update_ms, &out_perf->audio_tick_ms,
                          &out_perf->composite_ms, &out_perf->step_ms,
                          &out_perf->load_ms);
}

void
app_runtime_service_get_runtime_config(AppRuntimeConfig *out_config)
{
    if (!out_config) {
        return;
    }

    memset(out_config, 0, sizeof(*out_config));
    game_backend_get_runtime_config(&out_config->logic_divider,
                                    &out_config->audio_disabled,
                                    &out_config->composite_mode,
                                    &out_config->aot_enabled,
                                    &out_config->audio_backend,
                                    &out_config->refresh_rate_mode);

    if (!out_config->audio_backend || out_config->audio_backend[0] == '\0') {
        out_config->audio_backend = "native";
    }
}

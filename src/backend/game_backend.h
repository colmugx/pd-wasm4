#ifndef PLAYDATE_WASM4_GAME_BACKEND_H
#define PLAYDATE_WASM4_GAME_BACKEND_H

#include <stdbool.h>
#include <stddef.h>

#include "pd_api.h"

void
game_backend_init(PlaydateAPI *playdate);

void
game_backend_shutdown(void);

void
game_backend_on_pause(void);

void
game_backend_on_resume(void);

bool
game_backend_load(const char *path, float *out_load_ms, const char **out_error);

bool
game_backend_step(float *out_step_ms, const char **out_error);

void
game_backend_unload(void);

bool
game_backend_set_dither_mode(int mode, const char **out_error);

int
game_backend_get_dither_mode(void);

bool
game_backend_set_log_level(int level, const char **out_error);

bool
game_backend_set_refresh_rate_mode(int mode, float *out_refresh_rate,
                                   const char **out_error);

void
game_backend_get_fps(float *out_fps, float *out_refresh);

int
game_backend_rescan_carts(const char **out_current_path);

void
game_backend_list_carts(char *joined, size_t joined_size, int *out_count,
                        int *out_selected_index);

bool
game_backend_select_cart(int index, const char **out_path_or_error);

void
game_backend_get_status(bool *out_loaded, float *out_load_ms, float *out_step_ms,
                        const char **out_error, const char **out_current_path);

void
game_backend_get_perf(float *out_wasm_update_ms, float *out_audio_tick_ms,
                      float *out_composite_ms, float *out_step_ms,
                      float *out_load_ms);

void
game_backend_get_runtime_config(int *out_logic_divider, int *out_audio_disabled,
                                int *out_composite_mode, int *out_aot_enabled,
                                const char **out_audio_backend,
                                int *out_refresh_mode);

#endif

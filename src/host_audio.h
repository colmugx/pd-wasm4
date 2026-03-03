#ifndef PLAYDATE_WASM4_HOST_AUDIO_H
#define PLAYDATE_WASM4_HOST_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#ifndef WAMR_PD_AUDIO_BACKEND
#define WAMR_PD_AUDIO_BACKEND 1
#endif

#define WAMR_PD_AUDIO_BACKEND_WASM4_COMPAT 0
#define WAMR_PD_AUDIO_BACKEND_NATIVE 1

void
host_audio_init(void);

void
host_audio_reset(void);

void
host_audio_shutdown(void);

void
host_audio_set_paused(bool paused);

void
host_audio_tick(void);

void
host_audio_tone(int32_t frequency, int32_t duration, int32_t volume, int32_t flags);

int
host_audio_render(int16_t *left, int16_t *right, int len, bool output_enabled);

const char *
host_audio_backend_name(void);

#endif

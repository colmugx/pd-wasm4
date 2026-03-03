#ifndef PLAYDATE_WAMR_INPUT_MAPPER_H
#define PLAYDATE_WAMR_INPUT_MAPPER_H

#include <stdbool.h>
#include <stdatomic.h>

#include "pd_api.h"

#include "deps/wasm4/w4_memory_map.h"

void
input_mapper_clear_gamepads(W4MemoryMap *w4_mem);

void
input_mapper_update_gamepad(PlaydateAPI *pd, bool button_callback_enabled,
                            atomic_uint_fast32_t *button_down_mask,
                            atomic_uint_fast32_t *button_pressed_mask,
                            atomic_uint_fast32_t *button_released_mask,
                            bool *block_button1_until_release,
                            W4MemoryMap *w4_mem);

#endif

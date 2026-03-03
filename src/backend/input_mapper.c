#include "backend/input_mapper.h"

#include <stdint.h>

#define W4_BUTTON_1 1
#define W4_BUTTON_2 2
#define W4_BUTTON_LEFT 16
#define W4_BUTTON_RIGHT 32
#define W4_BUTTON_UP 64
#define W4_BUTTON_DOWN 128

void
input_mapper_clear_gamepads(W4MemoryMap *w4_mem)
{
    if (!w4_mem) {
        return;
    }

    w4_mem->gamepads[0] = 0;
    w4_mem->gamepads[1] = 0;
    w4_mem->gamepads[2] = 0;
    w4_mem->gamepads[3] = 0;
}

void
input_mapper_update_gamepad(PlaydateAPI *pd, bool button_callback_enabled,
                            atomic_uint_fast32_t *button_down_mask,
                            atomic_uint_fast32_t *button_pressed_mask,
                            atomic_uint_fast32_t *button_released_mask,
                            bool *block_button1_until_release,
                            W4MemoryMap *w4_mem)
{
    PDButtons current;
    PDButtons pushed;
    PDButtons released;
    uint32_t cb_current;
    uint32_t cb_pushed;
    uint32_t cb_released;
    uint8_t gamepad = 0;

    if (!pd || !pd->system || !w4_mem || !block_button1_until_release) {
        return;
    }

    pd->system->getButtonState(&current, &pushed, &released);
    if (button_callback_enabled && button_down_mask && button_pressed_mask
        && button_released_mask) {
        cb_current = atomic_load_explicit(button_down_mask, memory_order_relaxed);
        cb_pushed =
            atomic_exchange_explicit(button_pressed_mask, 0u, memory_order_relaxed);
        cb_released =
            atomic_exchange_explicit(button_released_mask, 0u, memory_order_relaxed);
        current = (PDButtons)((uint32_t)current | cb_current);
        pushed = (PDButtons)((uint32_t)pushed | cb_pushed);
        released = (PDButtons)((uint32_t)released | cb_released);
    }
    (void)pushed;

    if (*block_button1_until_release) {
        if ((released & kButtonA) || !(current & kButtonA)) {
            *block_button1_until_release = false;
        }
    }

    if ((current & kButtonA) && !*block_button1_until_release) {
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

    w4_mem->gamepads[0] = gamepad;
    w4_mem->gamepads[1] = 0;
    w4_mem->gamepads[2] = 0;
    w4_mem->gamepads[3] = 0;
}

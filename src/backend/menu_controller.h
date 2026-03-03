#ifndef PLAYDATE_WASM4_MENU_CONTROLLER_H
#define PLAYDATE_WASM4_MENU_CONTROLLER_H

#include <stdbool.h>

#include "pd_api.h"

typedef void (*MenuControllerActionFn)(void *userdata);
typedef bool (*MenuControllerSetRenderModeFn)(int mode, void *userdata);

typedef struct MenuController {
    PlaydateAPI *pd;
    PDMenuItem *menu_reload_item;
    PDMenuItem *menu_reset_item;
    PDMenuItem *menu_render_item;
    const char *render_mode_ptrs[8];
    const char **render_mode_labels;
    int render_mode_count;
    MenuControllerActionFn on_reload;
    MenuControllerActionFn on_reset;
    MenuControllerSetRenderModeFn on_set_render_mode;
    void *userdata;
} MenuController;

void
menu_controller_init(MenuController *controller);

void
menu_controller_configure(MenuController *controller, const char **render_mode_labels,
                          int render_mode_count, MenuControllerActionFn on_reload,
                          MenuControllerActionFn on_reset,
                          MenuControllerSetRenderModeFn on_set_render_mode,
                          void *userdata);

void
menu_controller_rebuild(MenuController *controller, PlaydateAPI *pd,
                        int render_mode);

void
menu_controller_sync_render_mode(MenuController *controller, PlaydateAPI *pd,
                                 int render_mode);

void
menu_controller_remove(MenuController *controller, PlaydateAPI *pd);

#endif

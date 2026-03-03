#include "backend/menu_controller.h"

#include <string.h>

#define MENU_CONTROLLER_MAX_RENDER_MODES 8

static void
menu_on_reload(void *userdata)
{
    MenuController *controller = (MenuController *)userdata;

    if (controller && controller->on_reload) {
        controller->on_reload(controller->userdata);
    }
}

static void
menu_on_reset(void *userdata)
{
    MenuController *controller = (MenuController *)userdata;

    if (controller && controller->on_reset) {
        controller->on_reset(controller->userdata);
    }
}

static void
menu_on_render(void *userdata)
{
    MenuController *controller = (MenuController *)userdata;
    int mode;

    if (!controller || !controller->menu_render_item || !controller->pd
        || !controller->pd->system || !controller->on_set_render_mode) {
        return;
    }

    mode = controller->pd->system->getMenuItemValue(controller->menu_render_item);
    (void)controller->on_set_render_mode(mode, controller->userdata);
}

void
menu_controller_init(MenuController *controller)
{
    if (!controller) {
        return;
    }

    memset(controller, 0, sizeof(*controller));
}

void
menu_controller_configure(MenuController *controller, const char **render_mode_labels,
                          int render_mode_count, MenuControllerActionFn on_reload,
                          MenuControllerActionFn on_reset,
                          MenuControllerSetRenderModeFn on_set_render_mode,
                          void *userdata)
{
    if (!controller) {
        return;
    }

    controller->render_mode_labels = render_mode_labels;
    controller->render_mode_count = render_mode_count;
    if (controller->render_mode_count > MENU_CONTROLLER_MAX_RENDER_MODES) {
        controller->render_mode_count = MENU_CONTROLLER_MAX_RENDER_MODES;
    }
    controller->on_reload = on_reload;
    controller->on_reset = on_reset;
    controller->on_set_render_mode = on_set_render_mode;
    controller->userdata = userdata;
}

void
menu_controller_remove(MenuController *controller, PlaydateAPI *pd)
{
    if (!controller || !pd || !pd->system) {
        return;
    }

    controller->pd = pd;

    if (controller->menu_reload_item) {
        pd->system->removeMenuItem(controller->menu_reload_item);
        controller->menu_reload_item = NULL;
    }
    if (controller->menu_reset_item) {
        pd->system->removeMenuItem(controller->menu_reset_item);
        controller->menu_reset_item = NULL;
    }
    if (controller->menu_render_item) {
        pd->system->removeMenuItem(controller->menu_render_item);
        controller->menu_render_item = NULL;
    }
}

void
menu_controller_rebuild(MenuController *controller, PlaydateAPI *pd,
                        int render_mode)
{
    int i;

    if (!controller || !pd || !pd->system) {
        return;
    }

    controller->pd = pd;
    menu_controller_remove(controller, pd);

    controller->menu_reload_item =
        pd->system->addMenuItem("Back To List", menu_on_reload, controller);
    controller->menu_reset_item =
        pd->system->addMenuItem("Reset", menu_on_reset, controller);

    for (i = 0; i < controller->render_mode_count; i++) {
        controller->render_mode_ptrs[i] = controller->render_mode_labels[i];
    }

    controller->menu_render_item = pd->system->addOptionsMenuItem(
        "Scale", controller->render_mode_ptrs, controller->render_mode_count,
        menu_on_render, controller);

    menu_controller_sync_render_mode(controller, pd, render_mode);
}

void
menu_controller_sync_render_mode(MenuController *controller, PlaydateAPI *pd,
                                 int render_mode)
{
    if (!controller || !pd || !pd->system || !controller->menu_render_item) {
        return;
    }

    pd->system->setMenuItemValue(controller->menu_render_item, render_mode);
}

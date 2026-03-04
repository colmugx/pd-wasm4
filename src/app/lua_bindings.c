#include "app/lua_bindings.h"

#include <stdbool.h>
#include <stddef.h>

#include "app/runtime_service.h"

static PlaydateAPI *pd;

static void
lua_push_error_or_nil(const char *message)
{
    if (message && message[0] != '\0') {
        pd->lua->pushString(message);
    }
    else {
        pd->lua->pushNil();
    }
}

static int
lua_return_bool_and_message(bool ok, const char *message)
{
    pd->lua->pushBool(ok ? 1 : 0);
    lua_push_error_or_nil(ok ? NULL : message);
    return 2;
}

static int
lua_wamr_load(lua_State *L)
{
    const char *path = NULL;
    const char *error = NULL;
    float load_ms = 0.0f;
    bool ok;

    (void)L;

    if (pd->lua->getArgCount() >= 1 && !pd->lua->argIsNil(1)) {
        path = pd->lua->getArgString(1);
    }

    ok = app_runtime_service_load(path, &load_ms, &error);
    pd->lua->pushBool(ok ? 1 : 0);
    pd->lua->pushFloat(load_ms);
    lua_push_error_or_nil(error);
    return 3;
}

static int
lua_wamr_step(lua_State *L)
{
    const char *error = NULL;
    float step_ms = 0.0f;
    bool ok;

    (void)L;

    ok = app_runtime_service_step(&step_ms, &error);
    pd->lua->pushBool(ok ? 1 : 0);
    pd->lua->pushFloat(step_ms);
    lua_push_error_or_nil(error);
    return 3;
}

static int
lua_wamr_unload(lua_State *L)
{
    (void)L;
    app_runtime_service_unload();
    return 0;
}

static int
lua_wamr_set_dither_mode(lua_State *L)
{
    const char *error = NULL;
    int mode;

    (void)L;

    if (pd->lua->getArgCount() < 1) {
        return lua_return_bool_and_message(false, "missing mode");
    }

    mode = pd->lua->getArgInt(1);
    if (!app_runtime_service_set_dither_mode(mode, &error)) {
        return lua_return_bool_and_message(false, error);
    }

    return lua_return_bool_and_message(true, NULL);
}

static int
lua_wamr_get_dither_mode(lua_State *L)
{
    (void)L;
    pd->lua->pushInt(app_runtime_service_get_dither_mode());
    return 1;
}

static int
lua_wamr_set_log_level(lua_State *L)
{
    const char *error = NULL;
    int level;

    (void)L;

    if (pd->lua->getArgCount() < 1) {
        return lua_return_bool_and_message(false, "missing level");
    }

    level = pd->lua->getArgInt(1);
    if (!app_runtime_service_set_log_level(level, &error)) {
        return lua_return_bool_and_message(false, error);
    }

    return lua_return_bool_and_message(true, NULL);
}

static int
lua_wamr_set_refresh_rate(lua_State *L)
{
    const char *error = NULL;
    float refresh_rate = 0.0f;
    int mode;

    (void)L;

    if (pd->lua->getArgCount() < 1) {
        return lua_return_bool_and_message(false, "missing mode");
    }

    mode = pd->lua->getArgInt(1);
    if (!app_runtime_service_set_refresh_rate_mode(mode, &refresh_rate, &error)) {
        return lua_return_bool_and_message(false, error);
    }

    pd->lua->pushBool(1);
    pd->lua->pushFloat(refresh_rate);
    return 2;
}

static int
lua_wamr_get_fps_raw(lua_State *L)
{
    float fps = 0.0f;
    float refresh = 0.0f;

    (void)L;

    app_runtime_service_get_fps(&fps, &refresh);
    pd->lua->pushFloat(fps);
    pd->lua->pushFloat(refresh);
    return 2;
}

static int
lua_wamr_rescan_carts(lua_State *L)
{
    const char *path = NULL;
    int count;

    (void)L;

    count = app_runtime_service_rescan_carts(&path);
    pd->lua->pushInt(count);
    pd->lua->pushString(path ? path : "");
    return 2;
}

static int
lua_wamr_list_carts(lua_State *L)
{
    char joined[APP_RUNTIME_CART_JOINED_CAPACITY];
    int count = 0;
    int selected_index = -1;

    (void)L;

    app_runtime_service_list_carts(joined, sizeof(joined), &count, &selected_index);
    pd->lua->pushInt(count);
    pd->lua->pushInt(selected_index);
    pd->lua->pushString(joined);
    return 3;
}

static int
lua_wamr_select_cart(lua_State *L)
{
    const char *path_or_error = NULL;
    int idx;
    bool ok;

    (void)L;

    if (pd->lua->getArgCount() < 1) {
        return lua_return_bool_and_message(false, "missing index");
    }

    idx = pd->lua->getArgInt(1);
    ok = app_runtime_service_select_cart(idx, &path_or_error);
    if (!ok) {
        return lua_return_bool_and_message(false, path_or_error);
    }

    pd->lua->pushBool(1);
    pd->lua->pushString(path_or_error ? path_or_error : "");
    return 2;
}

static int
lua_wamr_status_raw(lua_State *L)
{
    AppRuntimeStatus status;

    (void)L;

    app_runtime_service_get_status(&status);
    pd->lua->pushBool(status.loaded ? 1 : 0);
    pd->lua->pushFloat(status.load_ms);
    pd->lua->pushFloat(status.step_ms);
    lua_push_error_or_nil(status.error);
    pd->lua->pushString(status.current_path ? status.current_path : "");
    return 5;
}

static int
lua_wamr_perf_raw(lua_State *L)
{
    AppRuntimePerf perf;

    (void)L;

    app_runtime_service_get_perf(&perf);
    pd->lua->pushFloat(perf.wasm_update_ms);
    pd->lua->pushFloat(perf.audio_tick_ms);
    pd->lua->pushFloat(perf.composite_ms);
    pd->lua->pushFloat(perf.step_ms);
    pd->lua->pushFloat(perf.load_ms);
    return 5;
}

static int
lua_wamr_runtime_config_raw(lua_State *L)
{
    AppRuntimeConfig config;

    (void)L;

    app_runtime_service_get_runtime_config(&config);
    pd->lua->pushInt(config.logic_divider);
    pd->lua->pushBool(config.audio_disabled);
    pd->lua->pushInt(config.composite_mode);
    pd->lua->pushBool(config.aot_enabled);
    pd->lua->pushString(config.audio_backend ? config.audio_backend : "native");
    pd->lua->pushInt(config.refresh_rate_mode);
    pd->lua->pushBool(config.debug_output_enabled);
    return 7;
}

void
app_lua_bindings_register(PlaydateAPI *playdate)
{
    static const struct {
        int (*fn)(lua_State *L);
        const char *name;
    } lua_bindings[] = {
        { lua_wamr_load, "wamr_load" },
        { lua_wamr_step, "wamr_step" },
        { lua_wamr_unload, "wamr_unload" },
        { lua_wamr_status_raw, "wamr_status_raw" },
        { lua_wamr_perf_raw, "wamr_perf_raw" },
        { lua_wamr_runtime_config_raw, "wamr_runtime_config_raw" },
        { lua_wamr_set_dither_mode, "wamr_set_dither_mode" },
        { lua_wamr_get_dither_mode, "wamr_get_dither_mode" },
        { lua_wamr_set_log_level, "wamr_set_log_level" },
        { lua_wamr_set_refresh_rate, "wamr_set_refresh_rate" },
        { lua_wamr_get_fps_raw, "wamr_get_fps_raw" },
        { lua_wamr_rescan_carts, "wamr_rescan_carts" },
        { lua_wamr_list_carts, "wamr_list_carts" },
        { lua_wamr_select_cart, "wamr_select_cart" },
    };
    size_t i;
    const char *err;

    pd = playdate;

    for (i = 0; i < sizeof(lua_bindings) / sizeof(lua_bindings[0]); i++) {
        if (!pd->lua->addFunction(lua_bindings[i].fn, lua_bindings[i].name, &err)) {
            pd->system->logToConsole("[wasm4][error] addFunction %s failed: %s",
                                     lua_bindings[i].name, err);
        }
    }
}

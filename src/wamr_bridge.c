#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pd_api.h"
#include "wasm_export.h"

#define WAMR_DEFAULT_CART_PATH "cart/main.wasm"
#define WAMR_STACK_SIZE (64 * 1024)
#define WAMR_HEAP_SIZE (64 * 1024)

typedef struct WamrState {
    bool runtime_initialized;
    bool loaded;
    uint8_t *wasm_bytes;
    uint32_t wasm_size;
    wasm_module_t module;
    wasm_module_inst_t module_inst;
    wasm_exec_env_t exec_env;
    wasm_function_inst_t fn_init;
    wasm_function_inst_t fn_update;
    float last_load_ms;
    char last_error[256];
} WamrState;

static PlaydateAPI *pd;
static WamrState g_state;

static void
set_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_state.last_error, sizeof(g_state.last_error), fmt, args);
    va_end(args);
}

static void
clear_error(void)
{
    g_state.last_error[0] = '\0';
}

static int32_t
host_time_ms(wasm_exec_env_t exec_env)
{
    (void)exec_env;
    return (int32_t)pd->system->getCurrentTimeMilliseconds();
}

static void
host_log_i32(wasm_exec_env_t exec_env, int32_t value)
{
    (void)exec_env;
    pd->system->logToConsole("[wasm] %d", value);
}

static NativeSymbol g_native_symbols[] = {
    { "time_ms", host_time_ms, "()i", NULL },
    { "log_i32", host_log_i32, "(i)", NULL },
};

static void
cleanup_loaded_module(void)
{
    if (g_state.exec_env) {
        wasm_runtime_destroy_exec_env(g_state.exec_env);
        g_state.exec_env = NULL;
    }

    if (g_state.module_inst) {
        wasm_runtime_deinstantiate(g_state.module_inst);
        g_state.module_inst = NULL;
    }

    if (g_state.module) {
        wasm_runtime_unload(g_state.module);
        g_state.module = NULL;
    }

    if (g_state.wasm_bytes) {
        free(g_state.wasm_bytes);
        g_state.wasm_bytes = NULL;
    }

    g_state.wasm_size = 0;
    g_state.fn_init = NULL;
    g_state.fn_update = NULL;
    g_state.loaded = false;
}

static bool
ensure_runtime_initialized(void)
{
    if (g_state.runtime_initialized) {
        return true;
    }

    RuntimeInitArgs init_args;
    memset(&init_args, 0, sizeof(init_args));

    init_args.mem_alloc_type = Alloc_With_System_Allocator;
    init_args.native_module_name = "env";
    init_args.native_symbols = g_native_symbols;
    init_args.n_native_symbols =
        (uint32_t)(sizeof(g_native_symbols) / sizeof(g_native_symbols[0]));

    if (!wasm_runtime_full_init(&init_args)) {
        set_error("wasm_runtime_full_init failed");
        return false;
    }

    g_state.runtime_initialized = true;
    return true;
}

static bool
read_file_into_memory(const char *path, uint8_t **out_buf, uint32_t *out_size)
{
    FileStat st;
    SDFile *file;
    uint8_t *buf;
    uint32_t total_read = 0;
    int read_count;

    if (!path || !out_buf || !out_size) {
        set_error("invalid read_file arguments");
        return false;
    }

    if (pd->file->stat(path, &st) != 0) {
        set_error("stat failed for %s: %s", path, pd->file->geterr());
        return false;
    }

    if (st.isdir) {
        set_error("%s is a directory", path);
        return false;
    }

    if (st.size == 0) {
        set_error("%s is empty", path);
        return false;
    }

    file = pd->file->open(path, kFileReadData);
    if (!file) {
        file = pd->file->open(path, kFileRead);
    }
    if (!file) {
        set_error("open failed for %s: %s", path, pd->file->geterr());
        return false;
    }

    buf = malloc(st.size);
    if (!buf) {
        pd->file->close(file);
        set_error("malloc failed for %u bytes", st.size);
        return false;
    }

    while (total_read < st.size) {
        read_count = pd->file->read(file, buf + total_read, st.size - total_read);
        if (read_count <= 0) {
            break;
        }
        total_read += (uint32_t)read_count;
    }

    pd->file->close(file);

    if (total_read != st.size) {
        free(buf);
        set_error("read failed for %s (%u/%u)", path, total_read, st.size);
        return false;
    }

    *out_buf = buf;
    *out_size = st.size;
    return true;
}

static bool
call_i32_export(wasm_function_inst_t function, int32_t *out_ret)
{
    wasm_val_t results[1];

    memset(results, 0, sizeof(results));
    if (!wasm_runtime_call_wasm_a(g_state.exec_env, function, 1, results, 0,
                                  NULL)) {
        const char *exception = wasm_runtime_get_exception(g_state.module_inst);
        set_error("wasm exception: %s", exception ? exception : "unknown");
        return false;
    }

    if (out_ret) {
        *out_ret = results[0].of.i32;
    }
    return true;
}

static bool
load_wasm_cart(const char *path)
{
    char error_buf[256] = { 0 };
    uint32_t start_ms;
    uint32_t end_ms;
    int32_t init_ret = 0;

    cleanup_loaded_module();
    clear_error();

    start_ms = pd->system->getCurrentTimeMilliseconds();

    if (!read_file_into_memory(path, &g_state.wasm_bytes, &g_state.wasm_size)) {
        return false;
    }

    if (!ensure_runtime_initialized()) {
        cleanup_loaded_module();
        return false;
    }

    g_state.module = wasm_runtime_load(g_state.wasm_bytes, g_state.wasm_size,
                                       error_buf, sizeof(error_buf));
    if (!g_state.module) {
        set_error("wasm load failed: %s", error_buf);
        cleanup_loaded_module();
        return false;
    }

    g_state.module_inst = wasm_runtime_instantiate(g_state.module, WAMR_STACK_SIZE,
                                                   WAMR_HEAP_SIZE, error_buf,
                                                   sizeof(error_buf));
    if (!g_state.module_inst) {
        set_error("wasm instantiate failed: %s", error_buf);
        cleanup_loaded_module();
        return false;
    }

    g_state.exec_env =
        wasm_runtime_create_exec_env(g_state.module_inst, WAMR_STACK_SIZE);
    if (!g_state.exec_env) {
        set_error("wasm create exec env failed");
        cleanup_loaded_module();
        return false;
    }

    g_state.fn_init =
        wasm_runtime_lookup_function(g_state.module_inst, "cart_init");
    g_state.fn_update =
        wasm_runtime_lookup_function(g_state.module_inst, "cart_update");

    if (!g_state.fn_update) {
        g_state.fn_update =
            wasm_runtime_lookup_function(g_state.module_inst, "update");
    }
    if (!g_state.fn_init) {
        g_state.fn_init =
            wasm_runtime_lookup_function(g_state.module_inst, "init");
    }

    if (!g_state.fn_update) {
        set_error("missing export: cart_update (or update)");
        cleanup_loaded_module();
        return false;
    }

    if (g_state.fn_init) {
        if (!call_i32_export(g_state.fn_init, &init_ret)) {
            cleanup_loaded_module();
            return false;
        }
        if (init_ret != 0) {
            set_error("cart_init returned %d", (int)init_ret);
            cleanup_loaded_module();
            return false;
        }
    }

    end_ms = pd->system->getCurrentTimeMilliseconds();
    g_state.last_load_ms = (float)(end_ms - start_ms);
    g_state.loaded = true;
    clear_error();
    return true;
}

static bool
step_wasm_cart(void)
{
    int32_t update_ret = 0;

    if (!g_state.loaded || !g_state.fn_update) {
        set_error("no wasm loaded");
        return false;
    }

    if (!call_i32_export(g_state.fn_update, &update_ret)) {
        return false;
    }

    if (update_ret != 0) {
        set_error("cart_update returned %d", (int)update_ret);
        return false;
    }

    return true;
}

static int
lua_wamr_load(lua_State *L)
{
    const char *path = WAMR_DEFAULT_CART_PATH;
    bool ok;

    (void)L;

    if (pd->lua->getArgCount() >= 1 && !pd->lua->argIsNil(1)) {
        path = pd->lua->getArgString(1);
    }

    ok = load_wasm_cart(path);
    pd->lua->pushBool(ok ? 1 : 0);
    pd->lua->pushFloat(g_state.last_load_ms);
    if (ok) {
        pd->lua->pushNil();
    }
    else {
        pd->lua->pushString(g_state.last_error);
    }
    return 3;
}

static int
lua_wamr_step(lua_State *L)
{
    bool ok;

    (void)L;

    ok = step_wasm_cart();
    pd->lua->pushBool(ok ? 1 : 0);
    if (ok) {
        pd->lua->pushNil();
    }
    else {
        pd->lua->pushString(g_state.last_error);
    }
    return 2;
}

static int
lua_wamr_unload(lua_State *L)
{
    (void)L;
    cleanup_loaded_module();
    return 0;
}

static int
lua_wamr_status_raw(lua_State *L)
{
    (void)L;

    pd->lua->pushBool(g_state.loaded ? 1 : 0);
    pd->lua->pushFloat(g_state.last_load_ms);
    if (g_state.last_error[0] == '\0') {
        pd->lua->pushNil();
    }
    else {
        pd->lua->pushString(g_state.last_error);
    }
    return 3;
}

void
wamr_bridge_init(PlaydateAPI *playdate)
{
    const char *err;

    pd = playdate;
    memset(&g_state, 0, sizeof(g_state));

    if (!pd->lua->addFunction(lua_wamr_load, "wamr_load", &err)) {
        pd->system->logToConsole("addFunction wamr_load failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_step, "wamr_step", &err)) {
        pd->system->logToConsole("addFunction wamr_step failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_unload, "wamr_unload", &err)) {
        pd->system->logToConsole("addFunction wamr_unload failed: %s", err);
    }
    if (!pd->lua->addFunction(lua_wamr_status_raw, "wamr_status_raw", &err)) {
        pd->system->logToConsole("addFunction wamr_status_raw failed: %s", err);
    }
}

void
wamr_bridge_shutdown(void)
{
    cleanup_loaded_module();

    if (g_state.runtime_initialized) {
        wasm_runtime_destroy();
        g_state.runtime_initialized = false;
    }
}

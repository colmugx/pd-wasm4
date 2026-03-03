#include "backend/host_runtime.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "framebuffer.h"
#include "host_audio.h"
#include "util.h"

static HostRuntimeContext g_ctx;

static uint8_t *
current_memory(void)
{
    if (!g_ctx.w4_memory) {
        return NULL;
    }
    return *g_ctx.w4_memory;
}

static uint32_t
current_memory_size(void)
{
    if (!g_ctx.w4_memory_size) {
        return 0;
    }
    return *g_ctx.w4_memory_size;
}

static void
report_error_text(const char *message)
{
    if (g_ctx.set_error) {
        g_ctx.set_error(message ? message : "unknown error", g_ctx.userdata);
    }
}

static void
report_error_fmt(const char *fmt, ...)
{
    char buf[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    report_error_text(buf);
}

static void
report_log_fmt(int level, const char *fmt, ...)
{
    char buf[512];
    va_list args;

    if (!g_ctx.log_message) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    g_ctx.log_message(level, buf, g_ctx.userdata);
}

static void
set_host_exception(wasm_exec_env_t exec_env, const char *message)
{
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);

    if (inst) {
        wasm_runtime_set_exception(inst, message);
    }

    report_error_fmt("host exception: %s", message ? message : "unknown");
}

static bool
bounds_check_raw(const void *ptr, size_t size)
{
    const uint8_t *mem_start;
    const uint8_t *mem_end;
    const uint8_t *p;
    uint32_t mem_size = current_memory_size();

    if (!ptr) {
        return false;
    }

    mem_start = current_memory();
    if (!mem_start || mem_size == 0) {
        return false;
    }

    mem_end = mem_start + mem_size;
    p = (const uint8_t *)ptr;

    if (p < mem_start) {
        return false;
    }
    if (size > (size_t)(mem_end - p)) {
        return false;
    }
    return true;
}

static bool
bounds_check_cstr_raw(const char *str)
{
    const uint8_t *mem_start;
    const uint8_t *mem_end;
    const uint8_t *p;
    uint32_t mem_size = current_memory_size();

    if (!str) {
        return false;
    }

    mem_start = current_memory();
    if (!mem_start || mem_size == 0) {
        return false;
    }

    mem_end = mem_start + mem_size;
    p = (const uint8_t *)str;

    if (p < mem_start || p >= mem_end) {
        return false;
    }

    while (p < mem_end) {
        if (*p == '\0') {
            return true;
        }
        p++;
    }

    return false;
}

static bool
bounds_check(wasm_exec_env_t exec_env, const void *ptr, size_t size)
{
    if (!bounds_check_raw(ptr, size)) {
        set_host_exception(exec_env, "out of bounds memory access");
        return false;
    }
    return true;
}

static bool
bounds_check_cstr(wasm_exec_env_t exec_env, const char *str)
{
    if (!bounds_check_cstr_raw(str)) {
        set_host_exception(exec_env, "out of bounds string access");
        return false;
    }
    return true;
}

static bool
host_blit_sub_common(wasm_exec_env_t exec_env, const uint8_t *sprite, int32_t x,
                     int32_t y, int32_t width, int32_t height, int32_t src_x,
                     int32_t src_y, int32_t stride, int32_t flags)
{
    bool bpp2;
    uint64_t end_index;
    uint64_t bits;
    size_t bytes;

    if (width <= 0 || height <= 0) {
        return true;
    }

    if (src_x < 0 || src_y < 0 || stride <= 0) {
        set_host_exception(exec_env, "invalid blitSub args");
        return false;
    }

    if ((int64_t)src_x + (int64_t)width > (int64_t)stride) {
        set_host_exception(exec_env, "blitSub source rect exceeds stride");
        return false;
    }

    bpp2 = (flags & 1) != 0;
    end_index = (uint64_t)(uint32_t)(src_y + height - 1) * (uint64_t)(uint32_t)stride
                + (uint64_t)(uint32_t)(src_x + width - 1);
    bits = (end_index + 1) * (uint64_t)(bpp2 ? 2 : 1);
    bytes = (size_t)((bits + 7) / 8);

    if (!bounds_check(exec_env, sprite, bytes)) {
        return false;
    }

    w4_framebufferBlit(sprite, x, y, width, height, src_x, src_y, stride, bpp2,
                       (flags & 2) != 0, (flags & 4) != 0, (flags & 8) != 0);
    return true;
}

static void
host_blit(wasm_exec_env_t exec_env, const uint8_t *sprite, int32_t x, int32_t y,
          int32_t width, int32_t height, int32_t flags)
{
    (void)host_blit_sub_common(exec_env, sprite, x, y, width, height, 0, 0,
                               width, flags);
}

static void
host_blit_sub(wasm_exec_env_t exec_env, const uint8_t *sprite, int32_t x,
              int32_t y, int32_t width, int32_t height, int32_t src_x,
              int32_t src_y, int32_t stride, int32_t flags)
{
    (void)host_blit_sub_common(exec_env, sprite, x, y, width, height, src_x,
                               src_y, stride, flags);
}

static void
host_line(wasm_exec_env_t exec_env, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    (void)exec_env;
    w4_framebufferLine(x1, y1, x2, y2);
}

static void
host_hline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t len)
{
    (void)exec_env;
    w4_framebufferHLine(x, y, len);
}

static void
host_vline(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t len)
{
    (void)exec_env;
    w4_framebufferVLine(x, y, len);
}

static void
host_oval(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t width,
          int32_t height)
{
    (void)exec_env;
    w4_framebufferOval(x, y, width, height);
}

static void
host_rect(wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t width,
          int32_t height)
{
    (void)exec_env;
    w4_framebufferRect(x, y, width, height);
}

static void
host_text(wasm_exec_env_t exec_env, const char *str, int32_t x, int32_t y)
{
    if (!bounds_check_cstr(exec_env, str)) {
        return;
    }
    w4_framebufferText((const uint8_t *)str, x, y);
}

static void
host_text_utf8(wasm_exec_env_t exec_env, const uint8_t *str, int32_t byte_length,
               int32_t x, int32_t y)
{
    if (byte_length < 0) {
        set_host_exception(exec_env, "negative byteLength");
        return;
    }
    if (!bounds_check(exec_env, str, (size_t)byte_length)) {
        return;
    }
    w4_framebufferTextUtf8(str, byte_length, x, y);
}

static void
host_text_utf16(wasm_exec_env_t exec_env, const uint16_t *str,
                int32_t byte_length, int32_t x, int32_t y)
{
    if (byte_length < 0) {
        set_host_exception(exec_env, "negative byteLength");
        return;
    }
    if (!bounds_check(exec_env, str, (size_t)byte_length)) {
        return;
    }
    w4_framebufferTextUtf16(str, byte_length, x, y);
}

static void
host_tone(wasm_exec_env_t exec_env, int32_t frequency, int32_t duration,
          int32_t volume, int32_t flags)
{
    (void)exec_env;
    host_audio_tone(frequency, duration, volume, flags);
}

static int32_t
host_diskr(wasm_exec_env_t exec_env, uint8_t *dest, int32_t size)
{
    int32_t n;

    if (!g_ctx.disk) {
        set_host_exception(exec_env, "disk not available");
        return 0;
    }

    if (size < 0) {
        set_host_exception(exec_env, "negative disk size");
        return 0;
    }

    if (!bounds_check(exec_env, dest, (size_t)size)) {
        return 0;
    }

    n = size;
    if (n > g_ctx.disk->size) {
        n = g_ctx.disk->size;
    }

    memcpy(dest, g_ctx.disk->data, (size_t)n);
    return n;
}

static int32_t
host_diskw(wasm_exec_env_t exec_env, const uint8_t *src, int32_t size)
{
    int32_t n;

    if (!g_ctx.disk || !g_ctx.disk_dirty || !g_ctx.frames_since_disk_write) {
        set_host_exception(exec_env, "disk not available");
        return 0;
    }

    if (size < 0) {
        set_host_exception(exec_env, "negative disk size");
        return 0;
    }

    if (!bounds_check(exec_env, src, (size_t)size)) {
        return 0;
    }

    n = size;
    if (n > W4_DISK_MAX_BYTES) {
        n = W4_DISK_MAX_BYTES;
    }

    g_ctx.disk->size = (uint16_t)n;
    memcpy(g_ctx.disk->data, src, (size_t)n);
    *g_ctx.disk_dirty = true;
    *g_ctx.frames_since_disk_write = 0;

    return n;
}

static void
host_trace(wasm_exec_env_t exec_env, const char *str)
{
    if (!bounds_check_cstr(exec_env, str)) {
        return;
    }

    report_log_fmt(HOST_RUNTIME_LOG_INFO, "%s", str);
}

static void
host_trace_utf8(wasm_exec_env_t exec_env, const uint8_t *str, int32_t byte_length)
{
    char *buf;
    int32_t n;

    if (byte_length < 0) {
        set_host_exception(exec_env, "negative byteLength");
        return;
    }

    if (!bounds_check(exec_env, str, (size_t)byte_length)) {
        return;
    }

    n = byte_length;
    if (n > 2048) {
        n = 2048;
    }

    buf = malloc((size_t)n + 1);
    if (!buf) {
        return;
    }

    memcpy(buf, str, (size_t)n);
    buf[n] = '\0';
    report_log_fmt(HOST_RUNTIME_LOG_INFO, "%s", buf);
    free(buf);
}

static void
host_trace_utf16(wasm_exec_env_t exec_env, const uint16_t *str,
                 int32_t byte_length)
{
    if (byte_length < 0) {
        set_host_exception(exec_env, "negative byteLength");
        return;
    }

    if (!bounds_check(exec_env, str, (size_t)byte_length)) {
        return;
    }

    report_log_fmt(HOST_RUNTIME_LOG_INFO, "traceUtf16(len=%d)", (int)byte_length);
}

static void
append_char(char *out, size_t *len, size_t cap, char c)
{
    if (!out || !len || *len + 1 >= cap) {
        return;
    }

    out[*len] = c;
    (*len)++;
}

static void
append_cstr(char *out, size_t *len, size_t cap, const char *src)
{
    if (!src) {
        return;
    }

    while (*src) {
        append_char(out, len, cap, *src);
        src++;
    }
}

static void
append_fmt(char *out, size_t *len, size_t cap, const char *fmt, ...)
{
    va_list args;
    int n;

    if (!out || !len || *len >= cap) {
        return;
    }

    va_start(args, fmt);
    n = vsnprintf(out + *len, cap - *len, fmt, args);
    va_end(args);

    if (n < 0) {
        return;
    }

    if ((size_t)n >= cap - *len) {
        *len = cap - 1;
    }
    else {
        *len += (size_t)n;
    }
}

static void
host_tracef(wasm_exec_env_t exec_env, const char *fmt, const void *stack)
{
    const uint8_t *arg_ptr = (const uint8_t *)stack;
    char out[512];
    size_t out_len = 0;
    uint8_t *memory = current_memory();
    uint32_t memory_size = current_memory_size();

    if (!bounds_check_cstr(exec_env, fmt)) {
        return;
    }

    while (*fmt) {
        if (*fmt != '%') {
            append_char(out, &out_len, sizeof(out), *fmt++);
            continue;
        }

        fmt++;
        if (*fmt == '\0') {
            break;
        }

        switch (*fmt) {
            case '%':
                append_char(out, &out_len, sizeof(out), '%');
                break;
            case 'c':
                if (!bounds_check(exec_env, arg_ptr, 4)) {
                    return;
                }
                append_char(out, &out_len, sizeof(out), (char)w4_read32LE(arg_ptr));
                arg_ptr += 4;
                break;
            case 'd':
                if (!bounds_check(exec_env, arg_ptr, 4)) {
                    return;
                }
                append_fmt(out, &out_len, sizeof(out), "%d",
                           (int32_t)w4_read32LE(arg_ptr));
                arg_ptr += 4;
                break;
            case 'x':
                if (!bounds_check(exec_env, arg_ptr, 4)) {
                    return;
                }
                append_fmt(out, &out_len, sizeof(out), "%x",
                           (uint32_t)w4_read32LE(arg_ptr));
                arg_ptr += 4;
                break;
            case 's': {
                uint32_t ptr;
                const char *s;

                if (!bounds_check(exec_env, arg_ptr, 4)) {
                    return;
                }

                ptr = w4_read32LE(arg_ptr);
                arg_ptr += 4;

                if (ptr >= memory_size) {
                    set_host_exception(exec_env, "tracef string pointer out of bounds");
                    return;
                }

                s = (const char *)(memory + ptr);
                if (!bounds_check_cstr(exec_env, s)) {
                    return;
                }

                append_cstr(out, &out_len, sizeof(out), s);
                break;
            }
            case 'f':
                if (!bounds_check(exec_env, arg_ptr, 8)) {
                    return;
                }
                append_fmt(out, &out_len, sizeof(out), "%g", w4_readf64LE(arg_ptr));
                arg_ptr += 8;
                break;
            default:
                append_char(out, &out_len, sizeof(out), '%');
                append_char(out, &out_len, sizeof(out), *fmt);
                break;
        }

        fmt++;
    }

    out[out_len] = '\0';
    report_log_fmt(HOST_RUNTIME_LOG_INFO, "%s", out);
}

static NativeSymbol g_native_symbols[] = {
    { "blit", host_blit, "(*iiiii)", NULL },
    { "blitSub", host_blit_sub, "(*iiiiiiii)", NULL },
    { "line", host_line, "(iiii)", NULL },
    { "hline", host_hline, "(iii)", NULL },
    { "vline", host_vline, "(iii)", NULL },
    { "oval", host_oval, "(iiii)", NULL },
    { "rect", host_rect, "(iiii)", NULL },
    { "text", host_text, "($ii)", NULL },
    { "textUtf8", host_text_utf8, "(*~ii)", NULL },
    { "textUtf16", host_text_utf16, "(*~ii)", NULL },
    { "tone", host_tone, "(iiii)", NULL },
    { "diskr", host_diskr, "(*~)i", NULL },
    { "diskw", host_diskw, "(*~)i", NULL },
    { "trace", host_trace, "($)", NULL },
    { "traceUtf8", host_trace_utf8, "(*~)", NULL },
    { "traceUtf16", host_trace_utf16, "(*~)", NULL },
    { "tracef", host_tracef, "($*)", NULL },
};

void
host_runtime_bind_context(const HostRuntimeContext *ctx)
{
    if (!ctx) {
        memset(&g_ctx, 0, sizeof(g_ctx));
        return;
    }

    g_ctx = *ctx;
}

NativeSymbol *
host_runtime_get_native_symbols(uint32_t *out_count)
{
    if (out_count) {
        *out_count = (uint32_t)(sizeof(g_native_symbols) / sizeof(g_native_symbols[0]));
    }

    return g_native_symbols;
}

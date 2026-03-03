#include "platform_api_vmcore.h"

#include "pd_api.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

extern PlaydateAPI *g_playdate_api;

static void *
pd_realloc(void *ptr, size_t size)
{
    if (!g_playdate_api || !g_playdate_api->system
        || !g_playdate_api->system->realloc) {
        return NULL;
    }
    return g_playdate_api->system->realloc(ptr, size);
}

static void
pd_log_console(const char *message)
{
    if (g_playdate_api && g_playdate_api->system
        && g_playdate_api->system->logToConsole) {
        g_playdate_api->system->logToConsole("%s", message);
    }
}

static int
pd_format_and_log(const char *format, va_list ap)
{
    char buffer[256];
    int ret = vsnprintf(buffer, sizeof(buffer), format, ap);

    pd_log_console(buffer);
    return ret;
}

int
bh_platform_init(void)
{
    return 0;
}

void
bh_platform_destroy(void)
{}

void *
os_malloc(unsigned size)
{
    return pd_realloc(NULL, size);
}

void *
os_realloc(void *ptr, unsigned size)
{
    return pd_realloc(ptr, size);
}

void
os_free(void *ptr)
{
    (void)pd_realloc(ptr, 0);
}

int
os_printf(const char *format, ...)
{
    int ret;
    va_list ap;

    va_start(ap, format);
    ret = pd_format_and_log(format, ap);
    va_end(ap);

    return ret;
}

int
os_vprintf(const char *format, va_list ap)
{
    return pd_format_and_log(format, ap);
}

static uint64
clock_us(void)
{
    float ms;

    if (!g_playdate_api || !g_playdate_api->system
        || !g_playdate_api->system->getCurrentTimeMilliseconds) {
        return 0;
    }

    ms = g_playdate_api->system->getCurrentTimeMilliseconds();
    if (ms <= 0.0f) {
        return 0;
    }
    return (uint64)(ms * 1000.0f);
}

uint64
os_time_get_boot_us(void)
{
    return clock_us();
}

uint64
os_time_thread_cputime_us(void)
{
    return clock_us();
}

korp_tid
os_self_thread(void)
{
    return 0;
}

uint8 *
os_thread_get_stack_boundary(void)
{
    return NULL;
}

void
os_thread_jit_write_protect_np(bool enabled)
{
    (void)enabled;
}

int
os_mutex_init(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_mutex_destroy(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_mutex_lock(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_mutex_unlock(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    (void)hint;
    (void)prot;
    (void)flags;
    (void)file;
    return pd_realloc(NULL, size);
}

void
os_munmap(void *addr, size_t size)
{
    (void)size;
    (void)pd_realloc(addr, 0);
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    (void)addr;
    (void)size;
    (void)prot;
    return 0;
}

void
os_dcache_flush(void)
{}

void
os_icache_flush(void *start, size_t len)
{
    (void)start;
    (void)len;
}

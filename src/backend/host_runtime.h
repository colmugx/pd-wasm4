#ifndef PLAYDATE_WASM4_HOST_RUNTIME_H
#define PLAYDATE_WASM4_HOST_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "wasm_export.h"

#include "backend/persistence.h"

#define HOST_RUNTIME_LOG_ERROR 0
#define HOST_RUNTIME_LOG_INFO 1
#define HOST_RUNTIME_LOG_DEBUG 2

typedef void (*HostRuntimeSetErrorFn)(const char *message, void *userdata);
typedef void (*HostRuntimeLogMessageFn)(int level, const char *message,
                                        void *userdata);

typedef struct HostRuntimeContext {
    uint8_t **w4_memory;
    uint32_t *w4_memory_size;
    W4Disk *disk;
    bool *disk_dirty;
    uint32_t *frames_since_disk_write;
    HostRuntimeSetErrorFn set_error;
    HostRuntimeLogMessageFn log_message;
    void *userdata;
} HostRuntimeContext;

void
host_runtime_bind_context(const HostRuntimeContext *ctx);

NativeSymbol *
host_runtime_get_native_symbols(uint32_t *out_count);

#endif

#ifndef PLAYDATE_WASM4_PERSISTENCE_H
#define PLAYDATE_WASM4_PERSISTENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pd_api.h"

#define W4_DISK_MAX_BYTES 1024

typedef struct W4Disk {
    uint16_t size;
    uint8_t data[W4_DISK_MAX_BYTES];
} W4Disk;

typedef void (*PersistenceLogFileErrorFn)(const char *op, const char *path,
                                          const char *message, void *userdata);

void
persistence_derive_disk_path(const char *cart_path, char *out_path, size_t out_size);

void
persistence_load_for_cart(PlaydateAPI *pd, const char *cart_path, W4Disk *disk,
                          bool *disk_dirty, uint32_t *frames_since_disk_write,
                          char *disk_path, size_t disk_path_size,
                          PersistenceLogFileErrorFn log_file_error,
                          void *log_userdata);

bool
persistence_flush_if_needed(PlaydateAPI *pd, W4Disk *disk, bool *disk_dirty,
                            uint32_t *frames_since_disk_write,
                            const char *disk_path, bool force,
                            uint32_t flush_interval_frames,
                            PersistenceLogFileErrorFn log_file_error,
                            void *log_userdata,
                            char *error_buf, size_t error_buf_size);

#endif

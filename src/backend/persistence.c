#include "backend/persistence.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void
copy_cstr_trunc(char *dst, size_t dst_size, const char *src)
{
    size_t len;

    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void
set_error_text(char *error_buf, size_t error_buf_size, const char *fmt, ...)
{
    va_list args;

    if (!error_buf || error_buf_size == 0) {
        return;
    }

    va_start(args, fmt);
    vsnprintf(error_buf, error_buf_size, fmt, args);
    va_end(args);
}

static void
ensure_save_directory(PlaydateAPI *pd, PersistenceLogFileErrorFn log_file_error,
                      void *log_userdata)
{
    if (!pd || !pd->file) {
        return;
    }

    if (pd->file->mkdir("save") != 0) {
        const char *err = pd->file->geterr();
        if (err && err[0] != '\0' && log_file_error) {
            log_file_error("mkdir", "save", err, log_userdata);
        }
    }
}

void
persistence_derive_disk_path(const char *cart_path, char *out_path, size_t out_size)
{
    const char *base;
    char stem[64];
    size_t i = 0;

    base = strrchr(cart_path, '/');
    base = base ? base + 1 : cart_path;

    while (base[i] != '\0' && base[i] != '.' && i + 1 < sizeof(stem)) {
        char c = base[i];
        if (!isalnum((unsigned char)c) && c != '-' && c != '_') {
            c = '_';
        }
        stem[i] = c;
        i++;
    }
    stem[i] = '\0';

    if (stem[0] == '\0') {
        copy_cstr_trunc(stem, sizeof(stem), "cart");
    }

    snprintf(out_path, out_size, "save/%s.disk", stem);
}

void
persistence_load_for_cart(PlaydateAPI *pd, const char *cart_path, W4Disk *disk,
                          bool *disk_dirty, uint32_t *frames_since_disk_write,
                          char *disk_path, size_t disk_path_size,
                          PersistenceLogFileErrorFn log_file_error,
                          void *log_userdata)
{
    SDFile *file;
    int bytes_read;

    if (!pd || !pd->file || !cart_path || !disk || !disk_dirty
        || !frames_since_disk_write || !disk_path || disk_path_size == 0) {
        return;
    }

    memset(disk, 0, sizeof(*disk));
    *disk_dirty = false;
    *frames_since_disk_write = 0;

    ensure_save_directory(pd, log_file_error, log_userdata);
    persistence_derive_disk_path(cart_path, disk_path, disk_path_size);

    file = pd->file->open(disk_path, kFileReadData);
    if (!file) {
        file = pd->file->open(disk_path, kFileRead);
    }
    if (!file) {
        const char *err = pd->file->geterr();
        if (err && err[0] != '\0' && log_file_error) {
            log_file_error("open", disk_path, err, log_userdata);
        }
        return;
    }

    bytes_read = pd->file->read(file, disk->data, sizeof(disk->data));
    if (bytes_read < 0 && log_file_error) {
        log_file_error("read", disk_path, pd->file->geterr(), log_userdata);
    }
    if (bytes_read > 0) {
        disk->size = (uint16_t)bytes_read;
    }

    pd->file->close(file);
}

bool
persistence_flush_if_needed(PlaydateAPI *pd, W4Disk *disk, bool *disk_dirty,
                            uint32_t *frames_since_disk_write,
                            const char *disk_path, bool force,
                            uint32_t flush_interval_frames,
                            PersistenceLogFileErrorFn log_file_error,
                            void *log_userdata,
                            char *error_buf, size_t error_buf_size)
{
    SDFile *file;
    int written;

    if (!pd || !pd->file || !disk || !disk_dirty || !frames_since_disk_write
        || !disk_path) {
        set_error_text(error_buf, error_buf_size, "invalid persistence arguments");
        return false;
    }

    if (!*disk_dirty) {
        return true;
    }

    if (!force && *frames_since_disk_write < flush_interval_frames) {
        return true;
    }

    ensure_save_directory(pd, log_file_error, log_userdata);

    if (disk->size == 0) {
        if (pd->file->unlink(disk_path, 0) != 0) {
            if (log_file_error) {
                log_file_error("unlink", disk_path, pd->file->geterr(),
                               log_userdata);
            }
            set_error_text(error_buf, error_buf_size, "unlink disk failed for %s",
                           disk_path);
            return false;
        }
        *disk_dirty = false;
        *frames_since_disk_write = 0;
        return true;
    }

    file = pd->file->open(disk_path, kFileWrite);
    if (!file) {
        if (log_file_error) {
            log_file_error("open", disk_path, pd->file->geterr(), log_userdata);
        }
        set_error_text(error_buf, error_buf_size, "open disk failed for %s: %s",
                       disk_path, pd->file->geterr());
        return false;
    }

    written = pd->file->write(file, disk->data, disk->size);
    pd->file->close(file);

    if (written != disk->size) {
        if (log_file_error) {
            log_file_error("write", disk_path, pd->file->geterr(), log_userdata);
        }
        set_error_text(error_buf, error_buf_size, "write disk failed for %s",
                       disk_path);
        return false;
    }

    *disk_dirty = false;
    *frames_since_disk_write = 0;
    return true;
}

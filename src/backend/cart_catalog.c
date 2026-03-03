#include "backend/cart_catalog.h"

#include <stdio.h>
#include <stdlib.h>
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

static int
find_index_by_stem(const CartEntry *carts, int cart_count, const char *stem)
{
    int i;

    if (!stem || stem[0] == '\0') {
        return -1;
    }

    for (i = 0; i < cart_count; i++) {
        if (strcmp(carts[i].stem, stem) == 0) {
            return i;
        }
    }

    return -1;
}

static void
extract_stem(const char *name, char *out, size_t out_size)
{
    const char *dot;
    size_t len;

    if (!name || !out || out_size == 0) {
        return;
    }

    dot = strrchr(name, '.');
    if (!dot || dot == name) {
        copy_cstr_trunc(out, out_size, name);
        return;
    }

    len = (size_t)(dot - name);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, name, len);
    out[len] = '\0';
}

static bool
str_ends_with(const char *s, const char *suffix)
{
    size_t s_len;
    size_t suffix_len;

    if (!s || !suffix) {
        return false;
    }

    s_len = strlen(s);
    suffix_len = strlen(suffix);
    if (s_len < suffix_len) {
        return false;
    }
    return strcmp(s + s_len - suffix_len, suffix) == 0;
}

static const char *
filename_from_path(const char *path)
{
    const char *base;

    if (!path) {
        return "";
    }

    base = strrchr(path, '/');
    return base ? base + 1 : path;
}

static void
normalize_entry(CartEntry *entry)
{
    const char *display_path;
    const char *display_name;

    if (!entry) {
        return;
    }

    if (entry->aot_path[0] != '\0' && entry->wasm_path[0] != '\0') {
        display_path = entry->wasm_path;
        copy_cstr_trunc(entry->path, sizeof(entry->path), entry->wasm_path);
    }
    else if (entry->aot_path[0] != '\0') {
        display_path = entry->aot_path;
        copy_cstr_trunc(entry->path, sizeof(entry->path), entry->aot_path);
    }
    else if (entry->wasm_path[0] != '\0') {
        display_path = entry->wasm_path;
        copy_cstr_trunc(entry->path, sizeof(entry->path), entry->wasm_path);
    }
    else {
        entry->path[0] = '\0';
        entry->title[0] = '\0';
        return;
    }

    display_name = filename_from_path(display_path);
    copy_cstr_trunc(entry->title, sizeof(entry->title), display_name);
}

static int
entry_cmp(const void *a, const void *b)
{
    const CartEntry *ca = (const CartEntry *)a;
    const CartEntry *cb = (const CartEntry *)b;
    return strcmp(ca->title, cb->title);
}

const char *
cart_catalog_current_path(const CartEntry *carts, int cart_count, int selected_cart)
{
    if (!carts) {
        return "";
    }

    if (selected_cart >= 0 && selected_cart < cart_count) {
        return carts[selected_cart].path;
    }
    return "";
}

int
cart_catalog_find_index(const CartEntry *carts, int cart_count, const char *path)
{
    int i;

    if (!carts || !path) {
        return -1;
    }

    for (i = 0; i < cart_count; i++) {
        if (strcmp(carts[i].path, path) == 0
            || (carts[i].wasm_path[0] != '\0'
                && strcmp(carts[i].wasm_path, path) == 0)
            || (carts[i].aot_path[0] != '\0'
                && strcmp(carts[i].aot_path, path) == 0)) {
            return i;
        }
    }

    return -1;
}

void
cart_catalog_sort(CartEntry *carts, int cart_count)
{
    if (!carts || cart_count <= 1) {
        return;
    }

    qsort(carts, (size_t)cart_count, sizeof(carts[0]), entry_cmp);
}

bool
cart_catalog_collect_entry(PlaydateAPI *pd, const char *cart_dir, const char *name,
                           bool enable_aot, CartEntry *carts, int *inout_cart_count,
                           int max_carts)
{
    char full_path[W4_MAX_PATH_LEN];
    char stem[W4_MAX_PATH_LEN];
    FileStat st;
    SDFile *probe;
    int idx;
    bool is_wasm;
    bool is_aot;
    int cart_count;

    if (!pd || !pd->file || !name || !cart_dir || !carts || !inout_cart_count
        || max_carts <= 0) {
        return false;
    }

    is_wasm = str_ends_with(name, ".wasm");
    is_aot = str_ends_with(name, ".aot");
    if (!is_wasm && !is_aot) {
        return false;
    }
    if (is_aot && !enable_aot) {
        return false;
    }

    if (snprintf(full_path, sizeof(full_path), "%s/%s", cart_dir, name)
        >= (int)sizeof(full_path)) {
        return false;
    }

    probe = pd->file->open(full_path, kFileReadData);
    if (!probe) {
        return false;
    }
    pd->file->close(probe);

    if (pd->file->stat(full_path, &st) != 0 || st.isdir) {
        return false;
    }

    cart_count = *inout_cart_count;
    extract_stem(name, stem, sizeof(stem));
    idx = find_index_by_stem(carts, cart_count, stem);
    if (idx < 0) {
        idx = cart_count;
        if (idx >= max_carts) {
            return false;
        }

        memset(&carts[idx], 0, sizeof(carts[idx]));
        snprintf(carts[idx].stem, sizeof(carts[idx].stem), "%s", stem);
        cart_count++;
    }

    if (is_aot) {
        snprintf(carts[idx].aot_path, sizeof(carts[idx].aot_path), "%s", full_path);
    }
    else {
        snprintf(carts[idx].wasm_path, sizeof(carts[idx].wasm_path), "%s", full_path);
    }

    normalize_entry(&carts[idx]);
    *inout_cart_count = cart_count;
    return true;
}

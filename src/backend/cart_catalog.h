#ifndef PLAYDATE_WASM4_CART_CATALOG_H
#define PLAYDATE_WASM4_CART_CATALOG_H

#include <stdbool.h>

#include "pd_api.h"

#define W4_MAX_CARTS 64
#define W4_MAX_PATH_LEN 128

typedef struct CartEntry {
    char path[W4_MAX_PATH_LEN];
    char title[W4_MAX_PATH_LEN];
    char stem[W4_MAX_PATH_LEN];
    char wasm_path[W4_MAX_PATH_LEN];
    char aot_path[W4_MAX_PATH_LEN];
} CartEntry;

const char *
cart_catalog_current_path(const CartEntry *carts, int cart_count, int selected_cart);

int
cart_catalog_find_index(const CartEntry *carts, int cart_count, const char *path);

void
cart_catalog_sort(CartEntry *carts, int cart_count);

bool
cart_catalog_collect_entry(PlaydateAPI *pd, const char *cart_dir, const char *name,
                           bool enable_aot, CartEntry *carts, int *inout_cart_count,
                           int max_carts);

#endif

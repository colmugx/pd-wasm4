#ifndef PLAYDATE_WASM4_RENDER_COMPOSER_H
#define PLAYDATE_WASM4_RENDER_COMPOSER_H

#include <stdbool.h>

#include "pd_api.h"

#include "deps/wasm4/w4_memory_map.h"

void
render_composer_composite(PlaydateAPI *pd, W4MemoryMap *w4_mem, int render_mode,
                          int dither_mode, bool *framebuffer_clear_needed,
                          bool dirty_rows_valid, int dirty_src_row_min,
                          int dirty_src_row_max);

#endif

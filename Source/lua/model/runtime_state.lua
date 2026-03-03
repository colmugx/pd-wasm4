RuntimeState = RuntimeState or {}

RuntimeState.ditherNames = { "None", "Ordered" }
RuntimeState.statusSyncIntervalFrames = 6
RuntimeState.layout = {
    LIST_X = 18,
    LIST_Y = 50,
    LIST_W = 364,
    LIST_H = 136,
    ROW_H = 22,
}

RuntimeState.layout.VISIBLE_ROWS = math.floor(RuntimeState.layout.LIST_H / RuntimeState.layout.ROW_H)

function RuntimeState.clamp(v, lo, hi)
    if v < lo then
        return lo
    end
    if v > hi then
        return hi
    end
    return v
end

function RuntimeState.basename(path)
    if path == nil then
        return "unknown"
    end
    return string.match(path, "([^/]+)$") or path
end

function RuntimeState.new(bundleID)
    return {
        bundle_id = bundleID,
        loaded = false,
        load_ms = 0,
        step_ms = 0,
        wasm_update_ms = 0,
        audio_tick_ms = 0,
        composite_ms = 0,
        err = nil,
        path = "",
        dither_mode = 1,
        logic_divider = 1,
        audio_disabled = false,
        composite_mode = 0,
        aot_enabled = false,
        audio_backend = "native",
        refresh_rate_mode = 0,
        carts = {},
        selected_cart = 0,
        list_top = 1,
    }
end

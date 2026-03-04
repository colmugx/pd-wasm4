WamrApi = WamrApi or {}

function WamrApi.load(path)
    return wamr_load(path)
end

function WamrApi.step()
    return wamr_step()
end

function WamrApi.status()
    local loaded, load_ms, step_ms, err, path = wamr_status_raw()
    return {
        loaded = loaded,
        load_ms = load_ms,
        step_ms = step_ms,
        err = err,
        path = path,
    }
end

function WamrApi.perf()
    local wasm_update_ms, audio_tick_ms, composite_ms, step_ms, load_ms = wamr_perf_raw()
    return {
        wasm_update_ms = wasm_update_ms,
        audio_tick_ms = audio_tick_ms,
        composite_ms = composite_ms,
        step_ms = step_ms,
        load_ms = load_ms,
    }
end

function WamrApi.runtimeConfig()
    local logicDivider, audioDisabled, compositeMode, aotEnabled, audioBackend, refreshMode, debugOutputEnabled = wamr_runtime_config_raw()
    return {
        logic_divider = logicDivider or 1,
        audio_disabled = audioDisabled and true or false,
        composite_mode = compositeMode or 0,
        aot_enabled = aotEnabled and true or false,
        audio_backend = audioBackend or "native",
        refresh_rate_mode = refreshMode or 0,
        debug_output_enabled = debugOutputEnabled and true or false,
    }
end

function WamrApi.setDitherMode(mode)
    return wamr_set_dither_mode(mode)
end

function WamrApi.getDitherMode()
    return wamr_get_dither_mode()
end

function WamrApi.rescanCarts()
    return wamr_rescan_carts()
end

function WamrApi.listCarts()
    return wamr_list_carts()
end

function WamrApi.selectCart(index)
    return wamr_select_cart(index)
end

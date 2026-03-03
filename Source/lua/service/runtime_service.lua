RuntimeService = RuntimeService or {}

function RuntimeService.syncStatus(api, state, pollDither, pollPerf)
    local s = api.status()
    state.loaded = s.loaded
    state.load_ms = s.load_ms
    state.step_ms = s.step_ms
    state.err = s.err
    state.path = s.path or state.path
    if pollDither then
        state.dither_mode = api.getDitherMode() or state.dither_mode
    end
    if pollPerf then
        local perf = api.perf()
        state.wasm_update_ms = perf.wasm_update_ms or state.wasm_update_ms
        state.audio_tick_ms = perf.audio_tick_ms or state.audio_tick_ms
        state.composite_ms = perf.composite_ms or state.composite_ms
    end
end

function RuntimeService.syncRuntimeConfig(api, state)
    local cfg = api.runtimeConfig()
    state.logic_divider = cfg.logic_divider
    state.audio_disabled = cfg.audio_disabled
    state.composite_mode = cfg.composite_mode
    state.aot_enabled = cfg.aot_enabled
    state.audio_backend = cfg.audio_backend
    state.refresh_rate_mode = cfg.refresh_rate_mode
end

function RuntimeService.changeDither(api, state, delta, ditherNames)
    local count = #ditherNames
    local nextMode = ((state.dither_mode + delta) % count + count) % count
    local ok, err = api.setDitherMode(nextMode)
    if ok then
        state.dither_mode = nextMode
        state.err = nil
    else
        state.err = err
    end
end

function RuntimeService.currentModeLabel(state)
    local aotName = state.aot_enabled and "AOT:on" or "AOT:off"
    return string.format("L:%d %s", state.logic_divider, aotName)
end

function RuntimeService.step(api, state)
    local ok, step_ms, err = api.step()
    if ok then
        state.step_ms = step_ms
    else
        state.err = err
        RuntimeService.syncStatus(api, state, true, true)
    end
end

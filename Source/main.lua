import "CoreLibs/graphics"

local gfx = playdate.graphics
local font = gfx.getSystemFont()
gfx.setFont(font)

local selectedPath = "cart/main.wasm"

local wamr = {}

function wamr.load(path)
    return wamr_load(path)
end

function wamr.step()
    return wamr_step()
end

function wamr.unload()
    return wamr_unload()
end

function wamr.status()
    local loaded, last_ms, err = wamr_status_raw()
    return {
        loaded = loaded,
        last_ms = last_ms,
        err = err
    }
end

local state = {
    loaded = false,
    last_ms = 0,
    err = nil,
}

local function syncStatus()
    local s = wamr.status()
    state.loaded = s.loaded
    state.last_ms = s.last_ms
    state.err = s.err
end

local function drawUI()
    gfx.clear(gfx.kColorWhite)

    gfx.drawText("WAMR Cartridge Loader", 16, 16)
    gfx.drawText("A: load    B: unload", 16, 38)
    gfx.drawText("Path: " .. selectedPath, 16, 62)

    local statusText = state.loaded and "Loaded" or "NotLoaded"
    gfx.drawText("State: " .. statusText, 16, 88)
    gfx.drawText(string.format("Load: %.2f ms", state.last_ms), 16, 110)

    if state.err ~= nil then
        gfx.drawTextInRect("Err: " .. tostring(state.err), 16, 138, 368, 80)
    end

    playdate.drawFPS(320, 8)
end

function playdate.update()
    if playdate.buttonJustPressed(playdate.kButtonA) then
        local ok, ms, err = wamr.load(selectedPath)
        state.loaded = ok
        state.last_ms = ms
        state.err = err
    end

    if playdate.buttonJustPressed(playdate.kButtonB) then
        wamr.unload()
        syncStatus()
    end

    if state.loaded then
        local ok, err = wamr.step()
        if not ok then
            state.err = err
        end
    end

    drawUI()
end

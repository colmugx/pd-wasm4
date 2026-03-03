import "CoreLibs/graphics"

local gfx = playdate.graphics

local function loadFont(path, fallback)
    local f = gfx.font.new(path)
    if f == nil then
        return fallback
    end
    return f
end

local systemFont = gfx.getSystemFont()
local bundleID = (playdate.metadata and playdate.metadata.bundleID) or "<bundleID>"
local fonts = {
    title = loadFont("fonts/Roobert-20-Medium", systemFont),
    body = loadFont("fonts/Roobert-11-Medium", systemFont),
    mono = loadFont("fonts/Roobert-9-Mono-Condensed", systemFont),
}

local wamr = {}
local ditherNames = { "None", "Ordered" }

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
    local loaded, load_ms, step_ms, err, path = wamr_status_raw()
    return {
        loaded = loaded,
        load_ms = load_ms,
        step_ms = step_ms,
        err = err,
        path = path,
    }
end

function wamr.perf()
    local wasm_update_ms, audio_tick_ms, composite_ms, step_ms, load_ms = wamr_perf_raw()
    return {
        wasm_update_ms = wasm_update_ms,
        audio_tick_ms = audio_tick_ms,
        composite_ms = composite_ms,
        step_ms = step_ms,
        load_ms = load_ms,
    }
end

function wamr.runtimeConfig()
    local logicDivider, audioDisabled, compositeMode, aotEnabled, audioBackend, refreshMode = wamr_runtime_config_raw()
    return {
        logic_divider = logicDivider or 1,
        audio_disabled = audioDisabled and true or false,
        composite_mode = compositeMode or 0,
        aot_enabled = aotEnabled and true or false,
        audio_backend = audioBackend or "native",
        refresh_rate_mode = refreshMode or 0,
    }
end

function wamr.setLogLevel(level)
    return wamr_set_log_level(level)
end

function wamr.setRefreshRate(mode)
    return wamr_set_refresh_rate(mode)
end

function wamr.fpsRaw()
    local fps, refresh = wamr_get_fps_raw()
    return {
        fps = fps or 0,
        refresh = refresh or 0,
    }
end

function wamr.setDitherMode(mode)
    return wamr_set_dither_mode(mode)
end

function wamr.getDitherMode()
    return wamr_get_dither_mode()
end

function wamr.rescanCarts()
    return wamr_rescan_carts()
end

function wamr.listCarts()
    return wamr_list_carts()
end

function wamr.selectCart(index)
    return wamr_select_cart(index)
end

local state = {
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
local statusSyncIntervalFrames = 6
local statusSyncCounter = 0

local LIST_X = 18
local LIST_Y = 50
local LIST_W = 364
local LIST_H = 136
local ROW_H = 22
local VISIBLE_ROWS = math.floor(LIST_H / ROW_H)

local function basename(path)
    if path == nil then
        return "unknown"
    end
    return string.match(path, "([^/]+)$") or path
end

local function clamp(v, lo, hi)
    if v < lo then
        return lo
    end
    if v > hi then
        return hi
    end
    return v
end

local function ensureVisibleSelection()
    if #state.carts == 0 then
        state.list_top = 1
        return
    end

    if state.selected_cart < state.list_top then
        state.list_top = state.selected_cart
    end
    if state.selected_cart > state.list_top + VISIBLE_ROWS - 1 then
        state.list_top = state.selected_cart - VISIBLE_ROWS + 1
    end
    state.list_top = clamp(state.list_top, 1, math.max(1, #state.carts - VISIBLE_ROWS + 1))
end

local function syncStatus(pollDither, pollPerf)
    local s = wamr.status()
    state.loaded = s.loaded
    state.load_ms = s.load_ms
    state.step_ms = s.step_ms
    state.err = s.err
    state.path = s.path or state.path
    if pollDither then
        state.dither_mode = wamr.getDitherMode() or state.dither_mode
    end
    if pollPerf then
        local perf = wamr.perf()
        state.wasm_update_ms = perf.wasm_update_ms or state.wasm_update_ms
        state.audio_tick_ms = perf.audio_tick_ms or state.audio_tick_ms
        state.composite_ms = perf.composite_ms or state.composite_ms
    end
end

local function syncRuntimeConfig()
    local cfg = wamr.runtimeConfig()
    state.logic_divider = cfg.logic_divider
    state.audio_disabled = cfg.audio_disabled
    state.composite_mode = cfg.composite_mode
    state.aot_enabled = cfg.aot_enabled
    state.audio_backend = cfg.audio_backend
    state.refresh_rate_mode = cfg.refresh_rate_mode
end

local function cartHasAot(path)
    if path == nil or path == "" then
        return false
    end
    if string.match(path, "%.aot$") ~= nil then
        return true
    end
    local stem = string.match(path, "^(.*)%.wasm$")
    if stem == nil then
        return false
    end
    return playdate.file.exists(stem .. ".aot")
end

local function refreshCartList()
    local _, selectedIndex, joined = wamr.listCarts()
    local carts = {}

    for line in string.gmatch((joined or "") .. "\n", "(.-)\n") do
        if line ~= "" then
            local path, aotFlag = string.match(line, "^(.-)\t([01])$")
            if path == nil then
                path = line
            end
            table.insert(carts, {
                path = path,
                has_aot = aotFlag ~= nil and aotFlag == "1" or cartHasAot(path),
            })
        end
    end

    state.carts = carts
    if #state.carts == 0 then
        state.selected_cart = 0
        state.path = ""
        state.list_top = 1
        return
    end

    state.selected_cart = clamp((selectedIndex or 0) + 1, 1, #state.carts)
    state.path = state.carts[state.selected_cart].path
    ensureVisibleSelection()
end

local function applySelectedCart()
    if state.selected_cart < 1 or state.selected_cart > #state.carts then
        state.path = ""
        return
    end

    local ok, pathOrErr = wamr.selectCart(state.selected_cart - 1)
    if ok then
        state.path = pathOrErr
        state.err = nil
    else
        state.err = pathOrErr
    end
end

local function moveSelection(delta)
    if #state.carts == 0 then
        return
    end

    state.selected_cart = state.selected_cart + delta
    if state.selected_cart < 1 then
        state.selected_cart = #state.carts
    elseif state.selected_cart > #state.carts then
        state.selected_cart = 1
    end
    ensureVisibleSelection()
    applySelectedCart()
end

local function changeDither(delta)
    local count = #ditherNames
    local nextMode = ((state.dither_mode + delta) % count + count) % count
    local ok, err = wamr.setDitherMode(nextMode)
    if ok then
        state.dither_mode = nextMode
        state.err = nil
    else
        state.err = err
    end
end

local function loadSelectedCart()
    if #state.carts == 0 or state.path == nil or state.path == "" then
        state.loaded = false
        state.load_ms = 0
        state.err = string.format("No carts found. Add .wasm/.aot to Data/%s/cart", bundleID)
        return
    end

    local ok, load_ms, err = wamr.load(state.path)
    state.loaded = ok
    state.load_ms = load_ms
    state.err = err
    state.wasm_update_ms = 0
    state.audio_tick_ms = 0
    state.composite_ms = 0
end

local function currentModeLabel()
    local aotName = state.aot_enabled and "AOT:on" or "AOT:off"
    return string.format("L:%d %s", state.logic_divider, aotName)
end

local function drawTitleAndStatus()
    gfx.setFont(fonts.body)
    gfx.drawText("Playdate WASM-4", 22, 8)

    gfx.drawLine(16, 34, 384, 34)

    gfx.setFont(fonts.mono)
    gfx.drawText(string.format("Dither: %s", ditherNames[state.dither_mode + 1]), 22, 36)
    gfx.drawText(currentModeLabel(), 150, 36)
end

local function drawCartridgeList()
    local i

    gfx.setFont(fonts.body)
    gfx.drawRect(LIST_X, LIST_Y, LIST_W, LIST_H)

    if #state.carts == 0 then
        gfx.setFont(fonts.mono)
        gfx.drawTextAligned("No cartridges in Data/cart", LIST_X + LIST_W / 2, LIST_Y + 48, kTextAlignment.center)
        gfx.drawTextAligned(
            string.format("Copy .wasm/.aot to Data/%s/cart", bundleID),
            LIST_X + LIST_W / 2,
            LIST_Y + 68,
            kTextAlignment.center
        )
    end

    for i = 0, VISIBLE_ROWS - 1 do
        local idx = state.list_top + i
        local y = LIST_Y + i * ROW_H

        if idx > #state.carts then
            break
        end

        local cart = state.carts[idx]
        local label = basename(cart.path)
        if cart.has_aot then
            label = label .. " [AOT]"
        end
        if cart.path == state.path and state.loaded then
            label = "▶ " .. label
        else
            label = "  " .. label
        end

        if idx == state.selected_cart then
            gfx.fillRect(LIST_X + 1, y + 1, LIST_W - 2, ROW_H - 2)
            gfx.setImageDrawMode(gfx.kDrawModeFillWhite)
            gfx.drawText(label, LIST_X + 8, y + 3)
            gfx.setImageDrawMode(gfx.kDrawModeCopy)
        else
            gfx.drawText(label, LIST_X + 8, y + 3)
        end
    end

    gfx.setFont(fonts.mono)
    gfx.drawText(
        string.format("Carts %d  Selected %d/%d", #state.carts, (#state.carts == 0) and 0 or state.selected_cart, #state.carts),
        LIST_X + 4 , LIST_Y + LIST_H + 2
    )
end

local function drawFooter()
    gfx.drawLine(16, 204, 384, 204)
    gfx.setFont(fonts.mono)

    if #state.carts == 0 then
        gfx.drawText("No carts loaded       Ⓑ Rescan", 22, 210)
    else
        gfx.drawText("UP/DOWN Select       Ⓐ Run Ⓑ Rescan", 22, 210)
    end
    gfx.drawText("LEFT/RIGHT Dither", 22, 224)
end

local function drawBrowser()
    gfx.clear(gfx.kColorWhite)
    drawTitleAndStatus()
    drawCartridgeList()
    drawFooter()
    playdate.drawFPS(336, 2)
end

local runningNameCache = {
    text = nil,
    image = nil,
    x = 24,
    y = 0,
}

local function ensureRunningNameImage()
    local text = string.upper(basename(state.path):gsub("%.wasm$", ""):gsub("%.aot$", ""))
    if text == runningNameCache.text and runningNameCache.image ~= nil then
        return
    end

    gfx.setFont(fonts.body)
    local textW, textH = gfx.getTextSize(text)
    local src = gfx.image.new(math.max(1, textW + 2), math.max(1, textH + 2), gfx.kColorClear)
    if src == nil then
        runningNameCache.text = text
        runningNameCache.image = nil
        return
    end

    gfx.pushContext(src)
    gfx.clear(gfx.kColorClear)
    gfx.setColor(gfx.kColorBlack)
    gfx.drawText(text, 1, 1)
    gfx.popContext()

    -- Use one cached rotated bitmap for static HUD name; draw only this image each frame.
    local rotated = src:rotatedImage(-90)
    if rotated == nil then
        runningNameCache.text = text
        runningNameCache.image = nil
        return
    end

    local _, rotatedH = rotated:getSize()
    runningNameCache.text = text
    runningNameCache.image = rotated
    runningNameCache.y = 238 - rotatedH
end

local function drawRunningHUD()
    gfx.setFont(fonts.mono)
    gfx.setColor(gfx.kColorBlack)
    gfx.fillRect(0, 0, 80, 240)
    gfx.setColor(gfx.kColorWhite)
    gfx.fillRect(80, 0, 1, 240)
    gfx.setColor(gfx.kColorBlack)
    gfx.drawLine(319, 0, 319, 239)

    gfx.setImageDrawMode(gfx.kDrawModeFillWhite)
    gfx.setFont(fonts.body)
    ensureRunningNameImage()
    if runningNameCache.image ~= nil then
        runningNameCache.image:draw(runningNameCache.x, runningNameCache.y)
    end

    gfx.setImageDrawMode(gfx.kDrawModeCopy)
    gfx.setColor(gfx.kColorBlack)
    gfx.fillRect(320, 0, 80, 240)
    gfx.setImageDrawMode(gfx.kDrawModeFillWhite)
    gfx.setFont(fonts.mono)
    gfx.drawText("PLAYDATE", 328, 10)
    gfx.drawText("WASM4", 336, 24)
    gfx.drawText("FPS", 344, 46)
    playdate.drawFPS(336, 58)
    gfx.drawText(string.format("W %.2f", state.wasm_update_ms), 328, 82)
    gfx.drawText(string.format("A %.2f", state.audio_tick_ms), 328, 96)
    gfx.drawText(string.format("C %.2f", state.composite_ms), 328, 110)
    gfx.drawText(string.format("L%d", state.logic_divider), 336, 124)
    if state.audio_disabled then
        gfx.drawText("A OFF", 328, 138)
    end
    if state.composite_mode == 1 then
        gfx.drawText("C MIN", 328, 152)
    end
    gfx.setImageDrawMode(gfx.kDrawModeCopy)
end

local function stepAndDrawRunning()
    local ok, step_ms, err = wamr.step()
    if ok then
        state.step_ms = step_ms
    else
        state.err = err
        syncStatus(true, true)
    end
    drawRunningHUD()
end

local function handleBrowserInput()
    if playdate.buttonJustPressed(playdate.kButtonUp) then
        moveSelection(-1)
    end
    if playdate.buttonJustPressed(playdate.kButtonDown) then
        moveSelection(1)
    end
    if playdate.buttonJustPressed(playdate.kButtonLeft) then
        changeDither(-1)
    end
    if playdate.buttonJustPressed(playdate.kButtonRight) then
        changeDither(1)
    end
    if playdate.buttonJustPressed(playdate.kButtonA) then
        loadSelectedCart()
    end
    if playdate.buttonJustPressed(playdate.kButtonB) then
        wamr.rescanCarts()
        refreshCartList()
    end
end

refreshCartList()
applySelectedCart()
syncStatus(true, true)
syncRuntimeConfig()

function playdate.update()
    if state.loaded then
        statusSyncCounter = statusSyncCounter + 1
        if statusSyncCounter >= statusSyncIntervalFrames then
            syncStatus(true, true)
            statusSyncCounter = 0
        end
        stepAndDrawRunning()
    else
        syncStatus(true, true)
        statusSyncCounter = 0
        handleBrowserInput()
        if state.loaded then
            gfx.clear(gfx.kColorWhite)
            stepAndDrawRunning()
        else
            drawBrowser()
        end
    end
end

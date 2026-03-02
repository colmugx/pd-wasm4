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
    err = nil,
    path = "cart/main.wasm",
    dither_mode = 1,
    carts = {},
    selected_cart = 1,
    list_top = 1,
}

local LIST_X = 18
local LIST_Y = 60
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
    if state.selected_cart < state.list_top then
        state.list_top = state.selected_cart
    end
    if state.selected_cart > state.list_top + VISIBLE_ROWS - 1 then
        state.list_top = state.selected_cart - VISIBLE_ROWS + 1
    end
    state.list_top = clamp(state.list_top, 1, math.max(1, #state.carts - VISIBLE_ROWS + 1))
end

local function syncStatus()
    local s = wamr.status()
    state.loaded = s.loaded
    state.load_ms = s.load_ms
    state.step_ms = s.step_ms
    state.err = s.err
    state.path = s.path or state.path
    state.dither_mode = wamr.getDitherMode() or state.dither_mode
end

local function refreshCartList()
    local _, selectedIndex, joined = wamr.listCarts()
    local carts = {}

    for line in string.gmatch((joined or "") .. "\n", "(.-)\n") do
        if line ~= "" then
            table.insert(carts, line)
        end
    end

    if #carts == 0 then
        carts = { "cart/main.wasm" }
    end

    state.carts = carts
    state.selected_cart = clamp((selectedIndex or 0) + 1, 1, #state.carts)
    state.path = state.carts[state.selected_cart]
    ensureVisibleSelection()
end

local function applySelectedCart()
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
    local ok, load_ms, err = wamr.load(state.path)
    state.loaded = ok
    state.load_ms = load_ms
    state.err = err
end

local function drawTitleAndStatus()
    gfx.setFont(fonts.title)
    gfx.drawText("Cartridge Browser", 16, 8)

    gfx.drawLine(16, 34, 384, 34)

    gfx.setFont(fonts.body)
    gfx.drawText(string.format("State: %s", state.loaded and "Running" or "Idle"), 16, 40)
    gfx.drawText(string.format("Dither: %s", ditherNames[state.dither_mode + 1]), 160, 40)
    gfx.drawText(string.format("Load %.2fms  Step %.2fms", state.load_ms, state.step_ms), 250, 40)
end

local function drawCartridgeList()
    local i

    gfx.setFont(fonts.body)
    gfx.drawRect(LIST_X, LIST_Y, LIST_W, LIST_H)

    for i = 0, VISIBLE_ROWS - 1 do
        local idx = state.list_top + i
        local y = LIST_Y + i * ROW_H

        if idx > #state.carts then
            break
        end

        local label = basename(state.carts[idx])
        if state.carts[idx] == state.path and state.loaded then
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
        string.format("Carts %d  Selected %d/%d", #state.carts, state.selected_cart, #state.carts),
        LIST_X, LIST_Y + LIST_H + 4
    )
end

local function drawFooter()
    gfx.drawLine(16, 218, 384, 218)
    gfx.setFont(fonts.mono)

    if state.err ~= nil then
        gfx.drawText("Status: Error - " .. tostring(state.err), 16, 221)
    else
        gfx.drawText("Status: " .. tostring(state.path), 16, 221)
    end

    gfx.drawText("UP/DOWN Select  A Run  B Rescan  LEFT/RIGHT Dither", 16, 230)
end

local function drawBrowser()
    gfx.clear(gfx.kColorWhite)
    drawTitleAndStatus()
    drawCartridgeList()
    drawFooter()
    playdate.drawFPS(336, 2)
end

local function drawVerticalNameBottomToTop(text, x, bottomY, stepY)
    local i
    local y = bottomY

    for i = 1, #text do
        local ch = text:sub(i, i)
        y = y - stepY
        if y < 0 then
            break
        end
        gfx.drawText(ch, x, y)
    end
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
    drawVerticalNameBottomToTop(
        string.upper(basename(state.path):gsub("%.wasm$", "")),
        24,
        238,
        14
    )

    gfx.setImageDrawMode(gfx.kDrawModeCopy)
    gfx.setColor(gfx.kColorBlack)
    gfx.fillRect(320, 0, 80, 240)
    gfx.setImageDrawMode(gfx.kDrawModeFillWhite)
    gfx.setFont(fonts.mono)
    gfx.drawText("PLAYDATE", 328, 10)
    gfx.drawText("WASM4", 336, 24)
    gfx.drawText("FPS", 344, 46)
    playdate.drawFPS(336, 58)
    gfx.setImageDrawMode(gfx.kDrawModeCopy)
end

local function stepAndDrawRunning()
    local ok, step_ms, err = wamr.step()
    if ok then
        state.step_ms = step_ms
    else
        state.err = err
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
syncStatus()

function playdate.update()
    syncStatus()

    if state.loaded then
        stepAndDrawRunning()
    else
        handleBrowserInput()
        if state.loaded then
            gfx.clear(gfx.kColorWhite)
            stepAndDrawRunning()
        else
            drawBrowser()
        end
    end
end

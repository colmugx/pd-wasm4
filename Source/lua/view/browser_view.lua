BrowserView = BrowserView or {}

local gfx = playdate.graphics
local sprite = gfx.sprite

local browserCache = {
    background = nil,
    draw_state = nil,
    fonts = nil,
    dither_names = nil,
    layout = nil,
    carts_ref = nil,
    cart_count = -1,
    selected_cart = -1,
    list_top = -1,
    path = nil,
    loaded = nil,
    dither_mode = -1,
    logic_divider = -1,
    aot_enabled = nil,
}

local function resetSnapshot()
    browserCache.carts_ref = nil
    browserCache.cart_count = -1
    browserCache.selected_cart = -1
    browserCache.list_top = -1
    browserCache.path = nil
    browserCache.loaded = nil
    browserCache.dither_mode = -1
    browserCache.logic_divider = -1
    browserCache.aot_enabled = nil
end

local function captureSnapshot(state)
    browserCache.carts_ref = state.carts
    browserCache.cart_count = #state.carts
    browserCache.selected_cart = state.selected_cart
    browserCache.list_top = state.list_top
    browserCache.path = state.path
    browserCache.loaded = state.loaded
    browserCache.dither_mode = state.dither_mode
    browserCache.logic_divider = state.logic_divider
    browserCache.aot_enabled = state.aot_enabled
end

local function needsRedraw(state)
    if browserCache.carts_ref ~= state.carts then
        return true
    end
    if browserCache.cart_count ~= #state.carts then
        return true
    end
    if browserCache.selected_cart ~= state.selected_cart then
        return true
    end
    if browserCache.list_top ~= state.list_top then
        return true
    end
    if browserCache.path ~= state.path then
        return true
    end
    if browserCache.loaded ~= state.loaded then
        return true
    end
    if browserCache.dither_mode ~= state.dither_mode then
        return true
    end
    if browserCache.logic_divider ~= state.logic_divider then
        return true
    end
    if browserCache.aot_enabled ~= state.aot_enabled then
        return true
    end
    return false
end

local function canRedrawCursorRowsOnly(state)
    if browserCache.selected_cart == -1 then
        return false
    end
    if browserCache.carts_ref ~= state.carts then
        return false
    end
    if browserCache.cart_count ~= #state.carts then
        return false
    end
    if browserCache.list_top ~= state.list_top then
        return false
    end
    if browserCache.selected_cart == state.selected_cart then
        return false
    end
    if browserCache.loaded ~= state.loaded then
        return false
    end
    if browserCache.dither_mode ~= state.dither_mode then
        return false
    end
    if browserCache.logic_divider ~= state.logic_divider then
        return false
    end
    if browserCache.aot_enabled ~= state.aot_enabled then
        return false
    end
    if state.loaded and browserCache.path ~= state.path then
        return false
    end
    return true
end

local function addRowDirtyRect(layout, listTop, cartIndex)
    local row = cartIndex - listTop
    if row < 0 or row >= layout.VISIBLE_ROWS then
        return
    end

    local y = layout.LIST_Y + row * layout.ROW_H + 1
    sprite.addDirtyRect(layout.LIST_X + 1, y, layout.LIST_W - 2, layout.ROW_H - 2)
end

local function markCursorRowsDirty(layout, oldSelected, newSelected, listTop)
    addRowDirtyRect(layout, listTop, oldSelected)
    addRowDirtyRect(layout, listTop, newSelected)
end

local function drawTitleAndStatus(state, fonts, ditherNames)
    gfx.setFont(fonts.body)
    gfx.drawText("Playdate WASM-4", 22, 8)

    gfx.drawLine(16, 34, 384, 34)

    gfx.setFont(fonts.mono)
    gfx.drawText(string.format("Dither: %s", ditherNames[state.dither_mode + 1]), 22, 36)
    gfx.drawText(RuntimeService.currentModeLabel(state), 150, 36)
end

local function drawCartridgeList(state, fonts, layout)
    local i

    gfx.setFont(fonts.body)
    gfx.drawRect(layout.LIST_X, layout.LIST_Y, layout.LIST_W, layout.LIST_H)

    if #state.carts == 0 then
        gfx.setFont(fonts.mono)
        gfx.drawTextAligned("No cartridges in Data/cart", layout.LIST_X + layout.LIST_W / 2, layout.LIST_Y + 48, kTextAlignment.center)
        gfx.drawTextAligned(
            string.format("Copy .wasm/.aot to Data/%s/cart", state.bundle_id),
            layout.LIST_X + layout.LIST_W / 2,
            layout.LIST_Y + 68,
            kTextAlignment.center
        )
    end

    for i = 0, layout.VISIBLE_ROWS - 1 do
        local idx = state.list_top + i
        local y = layout.LIST_Y + i * layout.ROW_H

        if idx > #state.carts then
            break
        end

        local cart = state.carts[idx]
        local label = RuntimeState.basename(cart.path)
        if cart.has_aot then
            label = label .. " [AOT]"
        end
        if cart.path == state.path and state.loaded then
            label = "▶ " .. label
        else
            label = "  " .. label
        end

        if idx == state.selected_cart then
            gfx.fillRect(layout.LIST_X + 1, y + 1, layout.LIST_W - 2, layout.ROW_H - 2)
            gfx.setImageDrawMode(gfx.kDrawModeFillWhite)
            gfx.drawText(label, layout.LIST_X + 8, y + 3)
            gfx.setImageDrawMode(gfx.kDrawModeCopy)
        else
            gfx.drawText(label, layout.LIST_X + 8, y + 3)
        end
    end

    gfx.setFont(fonts.mono)
    gfx.drawText(
        string.format("Carts %d", #state.carts),
        layout.LIST_X + 4 , layout.LIST_Y + layout.LIST_H + 2
    )
end

local function drawFooter(state, fonts)
    gfx.drawLine(16, 204, 384, 204)
    gfx.setFont(fonts.mono)

    if #state.carts == 0 then
        gfx.drawText("No carts loaded       Ⓑ Rescan", 22, 210)
    else
        gfx.drawText("UP/DOWN Select       Ⓐ Run Ⓑ Rescan", 22, 210)
    end
    gfx.drawText("LEFT/RIGHT Dither", 22, 224)
end

local function ensureBackgroundSprite()
    if browserCache.background ~= nil then
        return
    end

    browserCache.background = sprite.setBackgroundDrawingCallback(function(x, y, width, height)
        local state = browserCache.draw_state
        local fonts = browserCache.fonts
        local ditherNames = browserCache.dither_names
        local layout = browserCache.layout
        if state == nil or fonts == nil or ditherNames == nil or layout == nil then
            return
        end

        gfx.setClipRect(x, y, width, height)
        gfx.setColor(gfx.kColorWhite)
        gfx.fillRect(x, y, width, height)
        gfx.setColor(gfx.kColorBlack)
        drawTitleAndStatus(state, fonts, ditherNames)
        drawCartridgeList(state, fonts, layout)
        drawFooter(state, fonts)
        gfx.clearClipRect()
    end)
end

function BrowserView.deactivate()
    if browserCache.background ~= nil then
        browserCache.background:remove()
        browserCache.background = nil
    end
    browserCache.draw_state = nil
    browserCache.fonts = nil
    browserCache.dither_names = nil
    browserCache.layout = nil
    resetSnapshot()
end

function BrowserView.draw(state, fonts, ditherNames, layout)
    ensureBackgroundSprite()

    browserCache.draw_state = state
    browserCache.fonts = fonts
    browserCache.dither_names = ditherNames
    browserCache.layout = layout

    if canRedrawCursorRowsOnly(state) then
        markCursorRowsDirty(layout, browserCache.selected_cart, state.selected_cart, state.list_top)
        captureSnapshot(state)
    elseif needsRedraw(state) then
        captureSnapshot(state)
        browserCache.background:markDirty()
    end

    sprite.update()
end

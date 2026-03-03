BrowserView = BrowserView or {}

local gfx = playdate.graphics

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
        string.format("Carts %d  Selected %d/%d", #state.carts, (#state.carts == 0) and 0 or state.selected_cart, #state.carts),
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

function BrowserView.draw(state, fonts, ditherNames, layout)
    gfx.clear(gfx.kColorWhite)
    drawTitleAndStatus(state, fonts, ditherNames)
    drawCartridgeList(state, fonts, layout)
    drawFooter(state, fonts)
    playdate.drawFPS(336, 2)
end

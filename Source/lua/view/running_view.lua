RunningView = RunningView or {}

local gfx = playdate.graphics

local runningNameCache = {
    text = nil,
    image = nil,
    x = 24,
    y = 0,
}

local function ensureRunningNameImage(state, fonts)
    local text = string.upper(RuntimeState.basename(state.path):gsub("%.wasm$", ""):gsub("%.aot$", ""))
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

function RunningView.drawHUD(state, fonts)
    gfx.setFont(fonts.mono)
    gfx.setColor(gfx.kColorBlack)
    gfx.fillRect(0, 0, 80, 240)
    gfx.setColor(gfx.kColorWhite)
    gfx.fillRect(80, 0, 1, 240)
    gfx.setColor(gfx.kColorBlack)
    gfx.drawLine(319, 0, 319, 239)

    gfx.setImageDrawMode(gfx.kDrawModeFillWhite)
    gfx.setFont(fonts.body)
    ensureRunningNameImage(state, fonts)
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

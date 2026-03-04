RunningView = RunningView or {}

local gfx = playdate.graphics

local HUD_PERF_REFRESH_INTERVAL = RuntimeState.statusSyncIntervalFrames or 6
local LEFT_PANEL_W = 80
local RIGHT_PANEL_X = 320
local RIGHT_PANEL_W = 80
local SCREEN_H = 240
local LEFT_SEPARATOR_X = 80
local RIGHT_SEPARATOR_X = 319
local RIGHT_DYNAMIC_X = 326
local RIGHT_DYNAMIC_Y = 56
local RIGHT_DYNAMIC_W = 72
local RIGHT_DYNAMIC_H = 112

local runningNameCache = {
    text = nil,
    image = nil,
    x = 24,
    y = 0,
}

local runningHUDCache = {
    initialized = false,
    perf_counter = 0,
    path = nil,
    debug_output_enabled = nil,
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

local function drawSeparators()
    gfx.setImageDrawMode(gfx.kDrawModeCopy)
    gfx.setColor(gfx.kColorWhite)
    gfx.drawLine(LEFT_SEPARATOR_X, 0, LEFT_SEPARATOR_X, SCREEN_H - 1)
    gfx.setColor(gfx.kColorBlack)
    gfx.drawLine(RIGHT_SEPARATOR_X, 0, RIGHT_SEPARATOR_X, SCREEN_H - 1)
end

local function drawLeftPanel(state, fonts)
    gfx.setImageDrawMode(gfx.kDrawModeCopy)
    gfx.setColor(gfx.kColorBlack)
    gfx.fillRect(0, 0, LEFT_PANEL_W, SCREEN_H)
    gfx.setImageDrawMode(gfx.kDrawModeFillWhite)
    gfx.setFont(fonts.body)
    ensureRunningNameImage(state, fonts)
    if runningNameCache.image ~= nil then
        runningNameCache.image:draw(runningNameCache.x, runningNameCache.y)
    end
    gfx.setImageDrawMode(gfx.kDrawModeCopy)
end

local function drawRightPanelStatic(fonts)
    gfx.setImageDrawMode(gfx.kDrawModeCopy)
    gfx.setColor(gfx.kColorBlack)
    gfx.fillRect(RIGHT_PANEL_X, 0, RIGHT_PANEL_W, SCREEN_H)
    gfx.setImageDrawMode(gfx.kDrawModeFillWhite)
    gfx.setFont(fonts.mono)
    gfx.drawText("PLAYDATE", 328, 10)
    gfx.drawText("WASM4", 336, 24)
    gfx.setImageDrawMode(gfx.kDrawModeCopy)
end

local function drawRightPanelDynamic(state, fonts)
    gfx.setImageDrawMode(gfx.kDrawModeCopy)
    gfx.setColor(gfx.kColorBlack)
    gfx.fillRect(RIGHT_DYNAMIC_X, RIGHT_DYNAMIC_Y, RIGHT_DYNAMIC_W, RIGHT_DYNAMIC_H)

    gfx.setImageDrawMode(gfx.kDrawModeFillWhite)
    gfx.setFont(fonts.mono)
    gfx.drawText("FPS", 344, 46)
    playdate.drawFPS(348, 60)

    if state.debug_output_enabled then
        gfx.drawText(string.format("W %.2f", state.wasm_update_ms), 328, 82)
        gfx.drawText(string.format("A %.2f", state.audio_tick_ms), 328, 96)
        gfx.drawText(string.format("C %.2f", state.composite_ms), 328, 110)
    end
    gfx.setImageDrawMode(gfx.kDrawModeCopy)
end

function RunningView.invalidateAll()
    runningHUDCache.initialized = false
    runningHUDCache.perf_counter = HUD_PERF_REFRESH_INTERVAL
    runningHUDCache.path = nil
    runningHUDCache.debug_output_enabled = nil
end

function RunningView.updateHUD(state, fonts)
    local needsFull = not runningHUDCache.initialized
    local pathChanged = runningHUDCache.path ~= state.path
    local debugChanged = runningHUDCache.debug_output_enabled ~= state.debug_output_enabled

    runningHUDCache.perf_counter = runningHUDCache.perf_counter + 1
    local perfTick = runningHUDCache.perf_counter >= HUD_PERF_REFRESH_INTERVAL

    drawSeparators()

    if needsFull or pathChanged then
        drawLeftPanel(state, fonts)
    end
    if needsFull or debugChanged then
        drawRightPanelStatic(fonts)
    end
    if needsFull or debugChanged or perfTick then
        drawRightPanelDynamic(state, fonts)
        runningHUDCache.perf_counter = 0
    end

    runningHUDCache.initialized = true
    runningHUDCache.path = state.path
    runningHUDCache.debug_output_enabled = state.debug_output_enabled
end

import "CoreLibs/graphics"
import "CoreLibs/sprites"

import "lua/api/wamr_api"
import "lua/model/runtime_state"
import "lua/service/cart_service"
import "lua/service/runtime_service"
import "lua/controller/browser_controller"
import "lua/controller/running_controller"
import "lua/view/ui_theme"
import "lua/view/browser_view"
import "lua/view/running_view"

local gfx = playdate.graphics
local bundleID = (playdate.metadata and playdate.metadata.bundleID) or "<bundleID>"
local fonts = UITheme.load(gfx)
local state = RuntimeState.new(bundleID)
local ditherNames = RuntimeState.ditherNames
local layout = RuntimeState.layout

local statusSyncCounter = 0

CartService.refreshCartList(WamrApi, state, layout.VISIBLE_ROWS)
CartService.applySelectedCart(WamrApi, state)
RuntimeService.syncRuntimeConfig(WamrApi, state)
RuntimeService.syncStatus(WamrApi, state, true, state.debug_output_enabled)
RunningView.invalidateAll()

function playdate.update()
    if state.loaded then
        BrowserView.deactivate()
        statusSyncCounter = statusSyncCounter + 1
        if statusSyncCounter >= RuntimeState.statusSyncIntervalFrames then
            RuntimeService.syncStatus(WamrApi, state, true, state.debug_output_enabled)
            statusSyncCounter = 0
        end
        RunningController.step(WamrApi, state)
        RunningView.updateHUD(state, fonts)
    else
        RunningView.invalidateAll()
        RuntimeService.syncStatus(WamrApi, state, true, false)
        statusSyncCounter = 0
        BrowserController.handleInput(WamrApi, state, {
            dither_names = ditherNames,
            visible_rows = layout.VISIBLE_ROWS,
        })
        if state.loaded then
            BrowserView.deactivate()
            gfx.clear(gfx.kColorWhite)
            RunningController.step(WamrApi, state)
            RunningView.updateHUD(state, fonts)
        else
            BrowserView.draw(state, fonts, ditherNames, layout)
        end
    end
end

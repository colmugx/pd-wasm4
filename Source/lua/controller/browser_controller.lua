BrowserController = BrowserController or {}

function BrowserController.handleInput(api, state, uiConfig)
    if playdate.buttonJustPressed(playdate.kButtonUp) then
        CartService.moveSelection(api, state, -1, uiConfig.visible_rows)
    end
    if playdate.buttonJustPressed(playdate.kButtonDown) then
        CartService.moveSelection(api, state, 1, uiConfig.visible_rows)
    end
    if playdate.buttonJustPressed(playdate.kButtonLeft) then
        RuntimeService.changeDither(api, state, -1, uiConfig.dither_names)
    end
    if playdate.buttonJustPressed(playdate.kButtonRight) then
        RuntimeService.changeDither(api, state, 1, uiConfig.dither_names)
    end
    if playdate.buttonJustPressed(playdate.kButtonA) then
        CartService.loadSelectedCart(api, state)
    end
    if playdate.buttonJustPressed(playdate.kButtonB) then
        api.rescanCarts()
        CartService.refreshCartList(api, state, uiConfig.visible_rows)
    end
end

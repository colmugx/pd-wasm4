CartService = CartService or {}

function CartService.ensureVisibleSelection(state, visibleRows)
    if #state.carts == 0 then
        state.list_top = 1
        return
    end

    if state.selected_cart < state.list_top then
        state.list_top = state.selected_cart
    end
    if state.selected_cart > state.list_top + visibleRows - 1 then
        state.list_top = state.selected_cart - visibleRows + 1
    end
    state.list_top = RuntimeState.clamp(state.list_top, 1, math.max(1, #state.carts - visibleRows + 1))
end

function CartService.cartHasAot(path)
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

function CartService.refreshCartList(api, state, visibleRows)
    local _, selectedIndex, joined = api.listCarts()
    local carts = {}

    for line in string.gmatch((joined or "") .. "\n", "(.-)\n") do
        if line ~= "" then
            local path, aotFlag = string.match(line, "^(.-)\t([01])$")
            if path == nil then
                path = line
            end
            table.insert(carts, {
                path = path,
                has_aot = aotFlag ~= nil and aotFlag == "1" or CartService.cartHasAot(path),
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

    state.selected_cart = RuntimeState.clamp((selectedIndex or 0) + 1, 1, #state.carts)
    state.path = state.carts[state.selected_cart].path
    CartService.ensureVisibleSelection(state, visibleRows)
end

function CartService.applySelectedCart(api, state)
    if state.selected_cart < 1 or state.selected_cart > #state.carts then
        state.path = ""
        return
    end

    local ok, pathOrErr = api.selectCart(state.selected_cart - 1)
    if ok then
        state.path = pathOrErr
        state.err = nil
    else
        state.err = pathOrErr
    end
end

function CartService.moveSelection(api, state, delta, visibleRows)
    if #state.carts == 0 then
        return
    end

    state.selected_cart = state.selected_cart + delta
    if state.selected_cart < 1 then
        state.selected_cart = #state.carts
    elseif state.selected_cart > #state.carts then
        state.selected_cart = 1
    end
    CartService.ensureVisibleSelection(state, visibleRows)
    CartService.applySelectedCart(api, state)
end

function CartService.loadSelectedCart(api, state)
    if #state.carts == 0 or state.path == nil or state.path == "" then
        state.loaded = false
        state.load_ms = 0
        state.err = string.format("No carts found. Add .wasm/.aot to Data/%s/cart", state.bundle_id)
        return
    end

    local ok, load_ms, err = api.load(state.path)
    state.loaded = ok
    state.load_ms = load_ms
    state.err = err
    state.wasm_update_ms = 0
    state.audio_tick_ms = 0
    state.composite_ms = 0
end

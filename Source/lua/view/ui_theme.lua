UITheme = UITheme or {}

local function loadFont(gfx, path, fallback)
    local f = gfx.font.new(path)
    if f == nil then
        return fallback
    end
    return f
end

function UITheme.load(gfx)
    local systemFont = gfx.getSystemFont()
    return {
        title = loadFont(gfx, "fonts/Roobert-20-Medium", systemFont),
        body = loadFont(gfx, "fonts/Roobert-11-Medium", systemFont),
        mono = loadFont(gfx, "fonts/Roobert-9-Mono-Condensed", systemFont),
    }
end

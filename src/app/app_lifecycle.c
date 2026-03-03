#include "app/app_lifecycle.h"

#include "app/lua_bindings.h"
#include "backend/game_backend.h"

void
app_lifecycle_init(PlaydateAPI *playdate)
{
    game_backend_init(playdate);
    app_lua_bindings_register(playdate);
}

void
app_lifecycle_shutdown(void)
{
    game_backend_shutdown();
}

void
app_lifecycle_on_pause(void)
{
    game_backend_on_pause();
}

void
app_lifecycle_on_resume(void)
{
    game_backend_on_resume();
}

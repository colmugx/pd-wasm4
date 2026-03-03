#include "wamr_bridge.h"

#include "app/app_lifecycle.h"

void
wamr_bridge_init(PlaydateAPI *playdate)
{
    app_lifecycle_init(playdate);
}

void
wamr_bridge_shutdown(void)
{
    app_lifecycle_shutdown();
}

void
wamr_bridge_on_pause(void)
{
    app_lifecycle_on_pause();
}

void
wamr_bridge_on_resume(void)
{
    app_lifecycle_on_resume();
}

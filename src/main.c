#include "pd_api.h"
#include "wamr_bridge.h"

PlaydateAPI *g_playdate_api = NULL;

#ifdef _WINDLL
__declspec(dllexport)
#endif
int
eventHandler(PlaydateAPI *playdate, PDSystemEvent event, uint32_t arg)
{
    (void)arg;
    g_playdate_api = playdate;

    switch (event) {
        case kEventInitLua:
            wamr_bridge_init(playdate);
            break;
        case kEventPause:
            wamr_bridge_on_pause();
            break;
        case kEventResume:
            wamr_bridge_on_resume();
            break;
        case kEventTerminate:
            wamr_bridge_shutdown();
            break;
        default:
            break;
    }

    return 0;
}

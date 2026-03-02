#include "pd_api.h"
#include "wamr_bridge.h"

#ifdef _WINDLL
__declspec(dllexport)
#endif
int
eventHandler(PlaydateAPI *playdate, PDSystemEvent event, uint32_t arg)
{
    (void)arg;

    if (event == kEventInitLua) {
        wamr_bridge_init(playdate);
    }
    else if (event == kEventTerminate) {
        wamr_bridge_shutdown();
    }

    return 0;
}

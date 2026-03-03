#ifndef PLAYDATE_WAMR_BRIDGE_H
#define PLAYDATE_WAMR_BRIDGE_H

#include "pd_api.h"

void
wamr_bridge_init(PlaydateAPI *playdate);

void
wamr_bridge_shutdown(void);

void
wamr_bridge_on_pause(void);

void
wamr_bridge_on_resume(void);

#endif

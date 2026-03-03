#ifndef PLAYDATE_WAMR_APP_LIFECYCLE_H
#define PLAYDATE_WAMR_APP_LIFECYCLE_H

#include "pd_api.h"

void
app_lifecycle_init(PlaydateAPI *playdate);

void
app_lifecycle_shutdown(void);

void
app_lifecycle_on_pause(void);

void
app_lifecycle_on_resume(void);

#endif

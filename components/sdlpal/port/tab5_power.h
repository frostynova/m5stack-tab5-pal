#pragma once

#include "common.h"

PAL_C_LINKAGE_BEGIN

typedef enum
{
   kTab5PowerUnknown = 0,
   kTab5PowerCharging,
   kTab5PowerDischarging,
   kTab5PowerIdle,
} TAB5POWERSTATE;

typedef struct
{
   BOOL valid;
   float voltage;
   float current;
   INT percent;
   TAB5POWERSTATE state;
} TAB5POWERSTATUS;

VOID PAL_Tab5PowerInit(VOID);
VOID PAL_Tab5PowerUpdate(VOID);
TAB5POWERSTATUS PAL_Tab5PowerGetStatus(VOID);

PAL_C_LINKAGE_END

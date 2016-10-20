#define INCL_WINSHELLDATA
#define INCL_DOSPROCESS
#define INCL_WINWINDOWMGR
#define INCL_DOSDEVICES
#include <os2.h>

#include "ejoyos2.h"
#include "joyos2.h"
#include "dualmode.h"

#ifndef DM_CONTROLS_H_INCLUDED
#define DM_CONTROLS_H_INCLUDED

extern HFILE hEJoy;
int ejoy_open();
void ejoy_close();
char ejoy_check_version();
void ejoy_ext_get_caps(LONG);
void ejoy_ext_get_info(LONG);

extern BOOL enhanced_joystick_interface;
extern GAME_EXT_INFO ejoy_info;
extern GAME_EXT_CAPS ejoy_caps;
extern GAME_EXT_CALIB ejoy_calib;
extern GAME_EXT_DEADZONE ejoy_deadzone;
extern char num_joysticks;

extern struct PMkey PMkeys[];

#endif

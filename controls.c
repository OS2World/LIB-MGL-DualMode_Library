#include "controls.h"
#include "dualmode.h"
#include "mglserver_internal.h"

struct PMkey PMkeys[ DEFINED_KEYS ] = {
/*   0 */	{ "Invalid",		0 },
/*   1 */	{ "ESC",		KB_esc },
/*   2 */	{ "1",			KB_1 },
/*   3 */	{ "2",			KB_2 },
/*   4 */	{ "3",			KB_3 },
/*   5 */	{ "4",			KB_4 },
/*   6 */	{ "5",			KB_5 },
/*   7 */	{ "6",			KB_6 },
/*   8 */	{ "7",			KB_7 },
/*   9 */	{ "8",			KB_8 },
/*  10 */	{ "9",			KB_9 },
/*  11 */	{ "0",			KB_0 },
/*  12 */	{ "-",			KB_minus },
/*  13 */	{ "=",			KB_equals },
/*  14 */	{ "Backspace",		KB_backspace },
/*  15 */	{ "TAB",		KB_tab },
/*  16 */	{ "Q",			KB_Q },
/*  17 */	{ "W",			KB_W },
/*  18 */	{ "E",			KB_E },
/*  19 */	{ "R",			KB_R },
/*  20 */	{ "T",			KB_T },
/*  21 */	{ "Y",			KB_Y },
/*  22 */	{ "U",			KB_U },
/*  23 */	{ "I",			KB_I },
/*  24 */	{ "O",			KB_O },
/*  25 */	{ "P",			KB_P },
/*  26 */	{ "[",			KB_leftSquareBrace },
/*  27 */	{ "]",			KB_rightSquareBrace },
/*  28 */	{ "Enter",		KB_enter },
/*  29 */	{ "Left control",	KB_leftCtrl },
/*  30 */	{ "A",			KB_A },
/*  31 */	{ "S",			KB_S },
/*  32 */	{ "D",			KB_D },
/*  33 */	{ "F",			KB_F },
/*  34 */	{ "G",			KB_G },
/*  35 */	{ "H",			KB_H },
/*  36 */	{ "J",			KB_J },
/*  37 */	{ "K",			KB_K },
/*  38 */	{ "L",			KB_L },
/*  39 */	{ "Semicolon",		KB_semicolon },
/*  40 */	{ "Apostrophe",		KB_apostrophe },
/*  41 */	{ "Tilde",		KB_tilde },
/*  42 */	{ "Left shift",		KB_leftShift },
/*  43 */	{ "Backslash",		KB_backSlash },
/*  44 */	{ "Z",			KB_Z },
/*  45 */	{ "X",			KB_X },
/*  46 */	{ "C",			KB_C },
/*  47 */	{ "V",			KB_V },
/*  48 */	{ "B",			KB_B },
/*  49 */	{ "N",			KB_N },
/*  50 */	{ "M",			KB_M },
/*  51 */	{ "Comma",		KB_comma },
/*  52 */	{ "Period",		KB_period },
/*  53 */	{ "Slash",		KB_divide },
/*  54 */	{ "Right shift",	KB_rightShift },
/*  55 */	{ "Keypad *",		KB_padTimes },
/*  56 */	{ "Left ALT",		KB_leftAlt },
/*  57 */	{ "Spacebar",		KB_space },
/*  58 */	{ "Caps Lock",		KB_capsLock },
/*  59 */	{ "F1",			KB_F1 },
/*  60 */	{ "F2",			KB_F2 },
/*  61 */	{ "F3",			KB_F3 },
/*  62 */	{ "F4",			KB_F4 },
/*  63 */	{ "F5",			KB_F5 },
/*  64 */	{ "F6",			KB_F6 },
/*  65 */	{ "F7",			KB_F7 },
/*  66 */	{ "F8",			KB_F8 },
/*  67 */	{ "F9",			KB_F9 },
/*  68 */	{ "F10",		KB_F10 },
/*  69 */	{ "Num lock",		KB_numLock },
/*  70 */	{ "Scroll lock",	KB_scrollLock },
/*  71 */	{ "Keypad 7",		KB_padHome },
/*  72 */	{ "Keypad 8",		KB_padUp },
/*  73 */	{ "Keypad 9",		KB_padPageUp },
/*  74 */	{ "Keypad -",		KB_padMinus },
/*  75 */	{ "Keypad 4",		KB_padLeft },
/*  76 */	{ "Keypad 5",		KB_padCenter },
/*  77 */	{ "Keypad 6",		KB_padRight },
/*  78 */	{ "Keypad +",		KB_padPlus },
/*  79 */	{ "Keypad 1",		KB_padEnd },
/*  80 */	{ "Keypad 2",		KB_padDown },
/*  81 */	{ "Keypad 3",		KB_padPageDown },
/*  82 */	{ "Keypad 0",		KB_padInsert },
/*  83 */	{ "Keypad period",	KB_padDelete },
/*  84 */	{ "Sys Request",	KB_sysReq },
/*  85 */	{ "?Key 85?",		85 },
/*  86 */	{ "?Key 86?",		86 },
/*  87 */	{ "F11",		KB_F11 },
/*  88 */	{ "F12",		KB_F12 },
/*  89 */	{ "?Key 89?",		89 },
/*  90 */	{ "Keypad Enter",	KB_padEnter },
/*  91 */	{ "Right control",	KB_rightCtrl },
/*  92 */	{ "Keypad /",		KB_padDivide },
/*  93 */	{ "Print Screen",	KB_sysReq },
/*  94 */	{ "Right ALT",		KB_rightAlt },
/*  95 */	{ "Pause - no MGL equivalent!",	95 },
/*  96 */	{ "Home",		KB_home },
/*  97 */	{ "Up arrow",		KB_up },
/*  98 */	{ "Page Up",		KB_pageUp },
/*  99 */	{ "Left arrow",		KB_left },
/* 100 */	{ "Right arrow",	KB_right },
/* 101 */	{ "End",		KB_end },
/* 102 */	{ "Down arrow",		KB_down },
/* 103 */	{ "Page Down",		KB_pageDown },
/* 104 */	{ "Insert",		KB_insert },
/* 105 */	{ "Delete",		KB_delete },
/* 106 */	{ "?Key 106?",		106 },
/* 107 */	{ "?Key 107?",		107 },
/* 108 */	{ "?Key 108?",		108 },
/* 109 */	{ "?Key 109?",		109 },
/* 110 */	{ "Break - no MGL equivalent!",	110 }
};

//
// JOYSTICK STUFF
//

HFILE hEJoy = 0;
BOOL enhanced_joystick_interface=FALSE, tried_jstick = FALSE;
GAME_EXT_INFO ejoy_info = {0};
GAME_EXT_CAPS ejoy_caps;
GAME_EXT_CALIB ejoy_calib;
GAME_EXT_DEADZONE ejoy_deadzone;
char num_joysticks = 0;

// Returns 0 on success.

int ejoy_open()
{
    ULONG  action = 0;
    APIRET rc;

    hEJoy = 0;
    tried_jstick = TRUE;

    rc = DosOpen(
                 (PUCHAR)GAMEPDDNAME,
                 &hEJoy,
                 &action,
                 0,
                 FILE_READONLY,
                 FILE_OPEN,
                 OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE,
                 NULL);

    return rc;
}

void ejoy_close()
{
  if ( hEJoy ) {
    DosClose(hEJoy);
  }

  hEJoy = 0;
}

char ejoy_check_version()
{
    ULONG version = 0;
    ULONG version_len = 0;

    version_len = sizeof(ULONG);

    DosDevIOCtl(hEJoy,
                IOCTL_CAT_USER, GAME_GET_VERSION,
                NULL, 0, NULL,
                &version, version_len, &version_len);

    enhanced_joystick_interface=FALSE;

    if (version >= 0x21)
    {
        enhanced_joystick_interface=TRUE;
        ejoy_ext_get_caps( 0 );
        num_joysticks = ejoy_caps.nro_of_buttons;
    }
    return enhanced_joystick_interface;
}

void ejoy_ext_get_caps( LONG whichstick )
{
    ULONG param;
    ULONG param_len;
    ULONG caps_len;

    caps_len = sizeof(GAME_EXT_CAPS);
    param_len = sizeof(ULONG);

    DosDevIOCtl(hEJoy,
                IOCTL_CAT_USER, GAME_EXT_GET_CAPS,
                &whichstick, param_len, &param_len,
                &ejoy_caps, caps_len, &caps_len);
}

void ejoy_ext_get_info( LONG whichstick )
{
    ULONG info_len;
    ULONG param_len;

    info_len = sizeof(GAME_EXT_INFO);
    param_len = sizeof(long);

    DosDevIOCtl(hEJoy,
                IOCTL_CAT_USER, GAME_EXT_GET_INFO,
                &whichstick, param_len, &param_len,
                &ejoy_info, info_len, &info_len);
}

//
// END JOYSTICK STUFF
//


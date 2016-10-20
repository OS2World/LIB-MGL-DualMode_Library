/*

This file contains information about new proposal for supporting
multiple joysticks, buttons and axis under OS/2. This file doesn't
contain information about IBM specification about interface 2.0.
If you want information about it try looking joyos2.h from somewhere.

Needed to change IOCTL category commands since GAMEPT.SYS was using 0x20
and also improved funtionality.

*/

#if !defined(__EJOYOS2_H__)
#define __EJOYOS2_H__

/* driver interface version number (2.1) */

#define GAME_EXTVERSION                 0x21

/* additional category commands in v2.1+ drivers */

// NOTE: 0x20 can't be used since GAMEPT.SYS is using it.
#define GAME_EXT_GET_DRIVER_VERSION     0x21
#define GAME_EXT_DRIVER_SPECIFIC        0x22
#define GAME_EXT_GET_CAPS               0x23
#define GAME_EXT_GET_INFO               0x24
#define GAME_EXT_SET_CALIB              0x25
#define GAME_EXT_GET_CALIB              0x26
#define GAME_EXT_SET_DEADZONE           0x27
#define GAME_EXT_GET_DEADZONE           0x28

/* joystick capabilites flags */

#define JOYCAPS_HASX                    0x0080
#define JOYCAPS_HASY                    0x0100
#define JOYCAPS_HASZ                    0x0001
#define JOYCAPS_HASR                    0x0002
#define JOYCAPS_HASU                    0x0004
#define JOYCAPS_HASV                    0x0008
#define JOYCAPS_HASPOV                  0x0010
#define JOYCAPS_POV4DIR                 0x0020
#define JOYCAPS_POVCTS                  0x0040
#define JOYCAPS_NOTCONNECTED            0x8000

/* button mask defines */

#define JOY_BUTTON1                     0x00000001l
#define JOY_BUTTON2                     0x00000002l
#define JOY_BUTTON3                     0x00000004l
#define JOY_BUTTON4                     0x00000008l
#define JOY_BUTTON5                     0x00000010l
#define JOY_BUTTON6                     0x00000020l
#define JOY_BUTTON7                     0x00000040l
#define JOY_BUTTON8                     0x00000080l
#define JOY_BUTTON9                     0x00000100l
#define JOY_BUTTON10                    0x00000200l
#define JOY_BUTTON11                    0x00000400l
#define JOY_BUTTON12                    0x00000800l
#define JOY_BUTTON13                    0x00001000l
#define JOY_BUTTON14                    0x00002000l
#define JOY_BUTTON15                    0x00004000l
#define JOY_BUTTON16                    0x00008000l
#define JOY_BUTTON17                    0x00010000l
#define JOY_BUTTON18                    0x00020000l
#define JOY_BUTTON19                    0x00040000l
#define JOY_BUTTON20                    0x00080000l
#define JOY_BUTTON21                    0x00100000l
#define JOY_BUTTON22                    0x00200000l
#define JOY_BUTTON23                    0x00400000l
#define JOY_BUTTON24                    0x00800000l
#define JOY_BUTTON25                    0x01000000l
#define JOY_BUTTON26                    0x02000000l
#define JOY_BUTTON27                    0x04000000l
#define JOY_BUTTON28                    0x08000000l
#define JOY_BUTTON29                    0x10000000l
#define JOY_BUTTON30                    0x20000000l
#define JOY_BUTTON31                    0x40000000l
#define JOY_BUTTON32                    0x80000000l

/* axis mask defines */

#define JOY_X_UP                        0x00000001l
#define JOY_X_DOWN                      0x00000002l
#define JOY_Y_UP                        0x00000004l
#define JOY_Y_DOWN                      0x00000008l
#define JOY_Z_UP                        0x00000010l
#define JOY_Z_DOWN                      0x00000020l
#define JOY_R_UP                        0x00000040l
#define JOY_R_DOWN                      0x00000080l
#define JOY_U_UP                        0x00000100l
#define JOY_U_DOWN                      0x00000200l
#define JOY_V_UP                        0x00000400l
#define JOY_V_DOWN                      0x00000800l

/* point of view state values (-1 if no pov) */

#define JOY_POVCENTERED                 (USHORT) -1
#define JOY_POVFORWARD                  0
#define JOY_POVRIGHT                    9000
#define JOY_POVBACKWARD                 18000
#define JOY_POVLEFT                     27000

typedef struct
{
    char driver_name[256];    /* driver name */
    char author_name[256];    /* author name of driver */
    char version_str[20];     /* version string */
    ULONG version;            /* version number (HI WORD major, LO WORD minor) */

} GAME_EXT_VERSION;

typedef struct
{
    char product[256];        /* product name */

    long nro_of_joysticks;    /* number of joysticks (usually 1) */

    long this_joystick_nro;   /* number of this joystick (usually 0) */
    ULONG flags;              /* capability flags */
    long nro_of_axis;         /* number of axis (0-6) */
    long nro_of_buttons;      /* number of buttons (0-32) */

} GAME_EXT_CAPS;

typedef struct
{
    long this_joystick_nro;   /* number of this joystick */

    ULONG xpos;               /* x position */
    ULONG ypos;               /* y position */
    ULONG zpos;               /* z position */
    ULONG rpos;               /* rudder / 4th axis position */
    ULONG upos;               /* 5th axis position */
    ULONG vpos;               /* 6th axis position */

    ULONG buttons;            /* bit mapped status of buttons */
    ULONG pov;                /* state of point of view */
    ULONG axis;               /* bitmapped statuses of axis */
} GAME_EXT_INFO;

typedef struct
{
    long this_joystick_nro;   /* number of this joystick */

    /* on pads axis should be set to these values */

    ULONG xmin, xcent, xmax;  /* min / center / max values of x axis */
    ULONG ymin, ycent, ymax;  /* min / center / max values of y axis */
    ULONG zmin, zcent, zmax;  /* min / center / max values of z axis */
    ULONG rmin, rcent, rmax;  /* min / center / max values of rudder / 4th axis */
    ULONG umin, ucent, umax;  /* min / center / max values of 5th axis */
    ULONG vmin, vcent, vmax;  /* min / center / max values of 6th axis */

} GAME_EXT_CALIB;

typedef struct
{
    long this_joystick_nro;   /* number of this joystick */

    ULONG x_dzone_lower, x_dzone_upper; /* lower and upper values of X deadzone */
    ULONG y_dzone_lower, y_dzone_upper; /* lower and upper values of Y deadzone */
    ULONG z_dzone_lower, z_dzone_upper; /* lower and upper values of Z deadzone */
    ULONG r_dzone_lower, r_dzone_upper; /* lower and upper values of R deadzone */
    ULONG u_dzone_lower, u_dzone_upper; /* lower and upper values of U deadzone */
    ULONG v_dzone_lower, v_dzone_upper; /* lower and upper values of V deadzone */

} GAME_EXT_DEADZONE;

#endif //!__EJOYOS2_H__

// this file created by Darrell Spice Jr. from the JOYPROG.DOC file in the
// JOYDOCS.ZIP archive included with the joystick sample
// the SpiceWare functions are listed at the end of this header file

#ifndef JOY_OS2
#define JOYOS2

#define GAMEPDDNAME "GAME$"

#define IOCTL_CAT_USER           0x80

#define GAME_GET_VERSION         0x01
#define GAME_GET_PARMS           0x02
#define GAME_SET_PARMS           0x03
#define GAME_GET_CALIB           0x04
#define GAME_SET_CALIB           0x05
#define GAME_GET_DIGSET          0x06
#define GAME_SET_DIGSET          0x07

#define GAME_GET_STATUS          0x10
#define GAME_GET_STATUS_BUTWAIT  0x11
#define GAME_GET_STATUS_SAMPWAIT 0x12


/* in use bitmasks originating in 1.0 */
#define GAME_USE_BOTH_OLDMASK 0x01 /* for backward compat with bool */
#define GAME_USE_X_NEWMASK 0x02
#define GAME_USE_Y_NEWMASK 0x04
#define GAME_USE_X_EITHERMASK (GAME_USE_X_NEWMASK|GAME_USE_BOTH_OLDMASK)
#define GAME_USE_Y_EITHERMASK (GAME_USE_Y_NEWMASK|GAME_USE_BOTH_OLDMASK)
#define GAME_USE_BOTH_NEWMASK (GAME_USE_X_NEWMASK|GAME_USE_Y_NEWMASK)

/* only timed sampling implemented in version 1.0 */
#define GAME_MODE_TIMED   1 /* timed sampling */
#define GAME_MODE_REQUEST 2 /* request driven sampling */

/* only raw implemented in version 1.0 */
#define GAME_DATA_FORMAT_RAW    1 /* [l,c,r]   */
#define GAME_DATA_FORMAT_SIGNED 2 /* [-l,0,+r] */
#define GAME_DATA_FORMAT_BINARY 3 /* {-1,0,+1} */
#define GAME_DATA_FORMAT_SCALED 4 /* [-10,+10] */

/* parameters defining the operation of the driver */
typedef struct
{
  USHORT useA;    /* new bitmasks: see above */
  USHORT useB;
  USHORT mode;    /* see consts above */
  USHORT format;  /* see consts above */
  USHORT sampDiv; /* samp freq = 32 / n */
  USHORT scale;   /* scaling factor */
  USHORT res1;    /* must be 0 */
  USHORT res2;    /* must be 0 */
} GAME_PARM_STRUCT;

/* 1-D position struct used for each axis */
typedef SHORT GAME_POS;  /* some data formats require signed values */

// struct to be used for calibration and digital response on each axis
typedef struct
{                    // calibration values for each axis:
  GAME_POS lower;    // - upper limit on value to be considered in lower range
  GAME_POS center;   // - center value
  GAME_POS upper;    // - lower limit on value to be considered in upper range
} GAME_3POS_STRUCT;

typedef struct
{
  GAME_3POS_STRUCT Ax;
  GAME_3POS_STRUCT Ay;
  GAME_3POS_STRUCT Bx;
  GAME_3POS_STRUCT By;
} GAME_CALIB_STRUCT;

// struct defining the digital response values for all axes
typedef struct
{
  GAME_3POS_STRUCT Ax;
  GAME_3POS_STRUCT Ay;
  GAME_3POS_STRUCT Bx;
  GAME_3POS_STRUCT By;
} GAME_DIGSET_STRUCT;

// simple 2-D position for each joystick
typedef struct
{
  GAME_POS x;
  GAME_POS y;
} GAME_2DPOS_STRUCT;

// struct defining the instantaneous state of both sticks and all buttons
typedef struct
{
  GAME_2DPOS_STRUCT A;
  GAME_2DPOS_STRUCT B;
  USHORT            butMask;
} GAME_DATA_STRUCT;

// status struct returned to OS/2 applications:
// current data for all sticks as well as button counts since last read
typedef struct
{
  GAME_DATA_STRUCT curdata;
  USHORT b1cnt;
  USHORT b2cnt;
  USHORT b3cnt;
  USHORT b4cnt;
} GAME_STATUS_STRUCT;



// SpiceWare Joystick Support Routines start here
typedef struct
{
  int Joy1X;    // Joystick 1's X value
  int Joy1Y;    //              Y value
  int Joy1A;    //              A Firebutton
  int Joy1B;    //              B Firebutton

  int Joy2X;    // Joystick 2's X Value
  int Joy2Y;    //              Y Value
  int Joy2A;    //              A Firebutton
  int Joy2B;    //              B Firebutton
} JOYSTICK_STATUS;

int JoystickInit(int); // 0 will load saved joystick calibration information from the file JOYSTICK.CLB(if it exists)
                       // 1 will set joystick to self-calibrate
int JoystickSaveCalibration(void); // will save the current joystick calibration information to the file JOYSTICK.CLB
int JoystickRange(int, int); // sets the inclusive low-high range for return values
int JoystickOn(void);  // enable the reading of the joystick values
int JoystickOff(void); // disable the reading of joystick values
int JoystickValues(JOYSTICK_STATUS *);// returns the values of the joysticks

#endif

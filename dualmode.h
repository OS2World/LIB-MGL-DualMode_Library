#ifndef DUALMODE_H_INCLUDED
#define DUALMODE_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#define INCL_WIN
#include <os2.h>

// Use the following definition to enable a debug log to be created for
// initialization of the client.  Note that this changes the size of the
// MGLSC_state structure below and will require an application to be
// recompiled to avoid structure alignment errors.
#define MGLSC_DEBUG_LOG

#include "dive.h"

#ifdef MGLSC_DEBUG_LOG
  #include <stdio.h>
#endif

struct vidmode_descrip
{
  int width, height;
  int depth;
};

struct MGLSC_state
{
  // DATA FOR INTERNAL USE ONLY
  // ==========================
  ULONG listener_thread, server_pid, pm_thread, timer_thread;
    // Thread IDs and process IDs
  ULONG MGL_session;
    // Session ID for full screen session (for use with DosSelectSession)
  void *shared_packet;
    // Packet used for communication with MGLServer
  HQUEUE command_queue, listener_queue;
    // Queues for communicating with MGLServer
  HEV client_wakeup;
    // Wake-up call for synchronous commands to MGLServer
  HEV framerate_set_sem;
    // Tells the frame rate timing thread to wake up and start timing
  HEV timer_tick_sem;
    // Posted when it's time to draw the next frame
  HMTX timing_mutex;
    // Can block the timing thread while frame rate is changing
  HDIVE diveinst;
    // DIVE instance
  ULONG divebufnum, depthfourcc;
    // DIVE buffer handle and the fourcc representation of current color depth
  int desktopdepth, desktopscan, desktopheight;
    // Desktop color depth and scan line length
  ULONG desktopFourCC;
    // Desktop color formatting
  int formatFlags;
    // Format flags passed to MGLSC_init
  int winposx, winposy, winsizex, winsizey;
    // Window position in absolute coordinates
  void *PMframebuffer;
    // Frame buffer returned by DiveOpen
  BOOL allowcustomblitter;
    // Set to 1 if custom DIVE blitters can be used on this system
  SETUP_BLITTER BlSet;
    // Parameters for PM DIVE operation
  BOOL palette_dirty;
    // Internal flag marking the palette as dirty
    // Shadow palette is contained in the shared packet
  int fswidth, fsheight, fsdepth;
    // Dimensions of the full screen video mode chosen
  char *winclassname, *wintitle;
  ULONG windowdecor;
  struct vidmode_descrip *allowedmodes;
    // Stored internally, but developer passes these in
  BOOL vidmode_reinit;
    // Flag that full screen session needs to change its video mode
    // (PM mode can change immediately, but full screen session needs
    //  to wait until it's reactivated to change.)
  BOOL disallow_blit;
    // Temporarily disallow blitting when screen mode is being changed.
  HFILE timer0;
  int framerate;
  int maxskip, curskip, laggedby;
    // Internally used frame rate regulation parameters.
  HWND keyDialog, keyCustomizeDialog;
  int activeKeyDef, keyDefInstruction, keyDefWaitCount;
  #ifdef MGLSC_DEBUG_LOG
    FILE *dbfp;
    // Debug log file handle
  #endif


  // DATA THAT COULD BE USEFUL FOR DEVELOPERS
  // ========================================
  BOOL autoBlitVidBufferOnRefresh;
    // Toggles whether or not the video buffer should automatically be
    // blitted when the screen mode changes.
  BOOL isFullScreen;
    // 1 if full screen session is active
  void *vidbuffer;
    // Video buffer.  Write your image HERE.
  HEV framesync_sem;
    // Posted when a blit has been completed
  int width, height, depth;
    // Dimensions of the video buffer
  HWND clientwin, framewin;
    // Client and frame window handles
  BOOL stretchblit;
    // Enable an image to be stretched as it is blitted in full screen mode
  BOOL custommodes;
    // Whether or not the use of custom on-the-fly video modes is allowed
};

typedef enum
{
  MGLSC_KEYBOARD_MAKE, MGLSC_KEYBOARD_BREAK, MGLSC_MOUSE_MOVE,
  MGLSC_MOUSE_BUTTON_PRESS, MGLSC_MOUSE_BUTTON_RELEASE,
  MGLSC_JOYSTICK_MOVE, MGLSC_JOYSTICK_BUTTON,
  MGLSC_JOYSTICK2_MOVE, MGLSC_JOYSTICK2_BUTTON
} InputEventType;

#define MGLSC_WINDOW_DECOR_MENU     1
#define MGLSC_WINDOW_DECOR_ICON     2
#define MGLSC_WINDOW_DECOR_TASKLIST 4
#define MGLSC_WINDOW_DECOR_MINMAX   8
#define MGLSC_WINDOW_DECOR_SYSMENU  16
#define MGLSC_WINDOW_DECOR_TITLEBAR 32

#define MGLSC_WINDOW_DECOR_PLAIN    0
#define MGLSC_WINDOW_DECOR_ALL      63

struct MGLSC_event_handlers
{
  MRESULT EXPENTRY (*AdditionalWindowProc) ( HWND, ULONG, MPARAM, MPARAM );
  // If this is set, it is called from the window procedure after normal window
  // processing is complete.

  void (*WindowInitializer) ( HWND );
  // This is called just after the window is created.  You can use this
  // time to enable or disable any menu items and set the window position
  // before it pops up.

  void (*ProcessInput) ( InputEventType, short, short );
  // In the keyboard messages, the first data parameter is the keyboard
  // scancode, and the second is unused.

  // In mouse and joystick movements the 2 data parameters are X and Y
  // position respectively.

  // In mouse and joystick button press messages, the first data parameter
  // is a bitmask of the button(s) pressed, and the second is unused.

  // Note that this function can be called by either the PM thread or the
  // listener queue thread (handling MGL input).  Don't rely on calling
  // any Win* API functions here.

  void (*WindowClosed) ( HWND );
  // Called on a WM_CLOSE message.  Use this opportunity to tell your core
  // to close up shop.  (Don't worry about closing the MGL session.  This
  // should be handled automatically.)

  void (*PMMenuAction) ( HWND, ULONG );
  // Handle any PM menu interaction here.  Command ID is passed in.
};

struct PMkey {
	char *description;
	unsigned char MGL_key;
};

#define DEFINED_KEYS 111

//
// Color format flags
//

#define FORMAT_DIRECTMAP   0 // Color values are direct RGB values
#define FORMAT_LOOKUPTABLE 1 // Color values are palette entries
#define FORMAT_RGB         0 // Direct color values are in order of RGB
#define FORMAT_BGR         2 // Direct color values are in order of BGR

//
// ERROR CODES from MGLSC_init
//
#define MGLSC_NO_ERROR                        0
#define MGLSC_UNSUPPORTED_COLOR_DEPTH         1
#define MGLSC_UNABLE_TO_FIND_BLITTER          2
#define MGLSC_UNABLE_TO_ALLOCATE_IMAGE_BUFFER 3

//
// API CALLS
// =========
//

int MGLSC_init( int width, int height, int depth, int formatFlags,
 int view_leftx, int view_topy, int view_rightx, int view_bottomy,
 struct MGLSC_event_handlers *eventhandler, char *winclassname,
 char *wintitle, struct vidmode_descrip *allowedmodes,
 BOOL windowDecorations, BOOL allowCustomModes,
 BOOL useJoystickIfAvailable );
// eventhandler is copied to the global struct.
// The mglclient global struct is cleared at the beginning of this call.
// You can customize the window class name and window titlebar text here.
// Note that the classname and window title strings should be
// permanently allocated somewhere before calling, not allocated on a
// stack frame that could be popped (use a global character array).
// The same is true of the allowedmodes structure.
// The allowedmodes structure should have the video modes in
// size-ascending order if you wish to get the "best fit" full screen
// video mode for the size you select.  Color depths should also be
// ascending.
// Format flags are described above in the "Color format flags" section.
// If you wish to enable joystick input, set useJoystickIfAvailable to 1
// and your ProcessInput function will receive data from the joystick
// every time MGLSC_flushUserInput (see below) is called.
// See MGLSC_setViewPort below for an explanation of the view_... parameters.

int MGLSC_reinit( int width, int height, int depth, int formatFlags,
 BOOL allowCustomModes, BOOL useJoystickIfAvailable );
// Change any of the above parameters after MGLSC_init has already been
// called without shutting everything down and restarting.  This call
// attempts to use the same viewport even if the video buffer size
// changes.  If the video buffer size becomes smaller than the viewport,
// then the viewport is made smaller.

struct MGLSC_state *MGLSC_clientState( void );
// Returns a pointer to much useful information about the state of this
// client.  You should avoid modifying any parameters in the top half of
// this structure unless you *really* know what you're doing.

void MGLSC_setViewPort( int view_leftx, int view_topy, int view_rightx,
 int view_bottomy, BOOL pickBestVidMode );
// Sets the viewable area of the video buffer.  This allows you to make
// a video buffer that is bigger than the displayed surface to improve
// performance by not requiring you to worry about clipping your images.
// You must post a WM_BLITFRAME message for this change to take effect.
// Illegal coordinate values will be rounded to the nearest legal value.
// If pickBestVidMode is non-zero, then the full screen video mode will
// be changed to provide the best fit for the viewport.  The mode will
// be selected out of the available modes passed to MGLSC_init.  If the
// viewport is made larger than the full screen mode size, the full
// screen mode will automatically be changed to the best fit mode
// regardless of the pickBestVidMode parameter.

void MGLSC_flushUserInput( void );
// Makes sure that user input is acknowledged and passed to the
// appropriate event handler routine.  This call returns immediately
// and does not wait for any user input to occur.  This should be called
// about once per frame.

const char * MGLSC_getKeyName( int PMkeycode );
// Returns a string describing the key passed in.  This is only really
// useful for GUI configuration dialogs, so it accepts key codes 
// received from WM_CHAR KC_SCANCODE messages.  If you want the name
// associated with an MGL key code, you need to call the translate function
// below.

int MGLSC_MGLkeyToPMKeycode( int MGLkeycode );
// Translates key codes received through ProcessInput or the MGL evt_
// API calls to equivalent PM scancodes.  Note that this involves a
// reverse-lookup and can be slow.  Don't use this too often if at all.

int MGLSC_PMkeyToMGLKeycode( int PMkeycode );
// Translates key codes received through a WM_CHAR KC_SCANCODE message
// to the equivalent MGL keycode.  This is not needed if the key code
// is obtained in your ProcessInput event.  These values are translated
// automatically.  This is only needed if you've got your own PM window
// getting keyboard input.

BOOL MGLSC_isJoystickPresent( void );
// Returns whether or not a supported joystick driver was detected.

BOOL MGLSC_getNumJoysticks( void );
// Returns the number of joysticks present.

BOOL MGLSC_getNumJoyButtons( LONG whichStick );
// Returns the number of buttons on the joystick selected (0-based).

void MGLSC_mouseGrab( BOOL yesNo );
// When yesNo is non-zero, this causes the application to grab control of
// the mouse pointer, make the system mouse cursor invisible, and accept
// and log mouse movement and button information.  Mouse grabbing is
// automatically activated when in full screen mode, so this setting only
// applies to PM operation.  You will not receive mouse input in PM mode
// unless mouseGrab is activated.  Mouse grabbing is automatically
// deactivated if the window focus changes in PM mode.  On startup, this
// library defaults to having grabbing deactivated.

void MGLSC_setMouseModeRelative( BOOL isRelative );
// Toggles whether mouse movement should be given as relative or absolute
// measures of mouse position.  When relative mode is set on, only the
// change in the mouse position since the last event flush will be
// returned.  When absolute mode is specified (by passing a 0 to this
// function), an absolute position is specified in terms of the size of
// the playfield.  On startup, this library defaults to relative mode.
// This function can also be used to reset the absolute position back to
// the center of the playfield if absolute mode is selected.

void MGLSC_setMousePosition( int x, int y );
// Sets the mouse position to the desired location.  Note that these
// coordinates are in terms of the size of the viewport.

void MGLSC_setColor( int coloridx, int red, int green, int blue );
// Sets the color at coloridx to the desired red, green, and blue values.

void MGLSC_getColor( int coloridx, int *red, int *green, int *blue );
// Gets the color at coloridx to the desired red, green, and blue values.

void MGLSC_setTimingMode( BOOL useTimer0 );
// Changes whether or not the Timer0 device driver or DosSleep
// is used for frame timing (defaults to DosSleep).
// Can be changed on-the-fly.

void MGLSC_setFrameRate( int frames_per_second );
// Sets the frame rate.  This function must be called at least once to use
// any of the frame rate regulation API calls.  To properly shut down and
// clean up the frame rate regulation code, call this function with a
// parameter of 0.

void MGLSC_setMaxFrameSkip( int max_frames_to_skip );
// Skips no more than x frames in a row (defaults to 3 if not called).

void MGLSC_blockUntilFrameReady( void );
// Blocks the calling thread until it's time for the next frame.

BOOL MGLSC_shouldSkipFrame( void );
// Returns TRUE if your application should skip drawing this frame
// in order to maintain the frame rate.

void MGLSC_advanceFrame( BOOL skipped );
// Tells the frame skip logic that this frame has finished rendering.
// This will tell the frame skip algorithm and frame throttle algorithm to
// consider all queries and requests after this point from the perspective of
// the next frame drawn.  This should be called after all
// MGLSC_shouldSkipFrame and MGLSC_blockUntilFrameReady calls are made for
// this frame to move on to the next one.  Pass TRUE into this function if
// this frame was not drawn, otherwise pass it FALSE.

void MGLSC_resetSkipParams( void );
// Call this if your application has temporarily stopped drawing frames and
// calling MGLSC_advanceFrame for a while, but wants to start again.  This
// will prevent excessive frameskip from occurring after restarting to draw
// frames.

void MGLSC_shutdown( void );
// Closes the main program window and waits for MGLServer to shut down if
// it has been started.

void MGLSC_waitForShutdown( void );
// Waits for the PM thread (and MGLServer) to shut down.

void MGLSC_loadCustomKeyboard( char *iniName, char *appName, char *keyName );
// Loads the customized keyboard settings from the INI file specified.
// appName and keyName correspond to the appropriate fields in the INI file.

void MGLSC_saveCustomKeyboard( char *iniName, char *appName, char *keyName );
// Saves the customized keyboard settings to the INI file specified.
// appName and keyName correspond to the appropriate fields in the INI file.

void MGLSC_customizeKeyboard( void );
// Brings up the dialog windows that allow the user to customize the keyboard
// settings.  Any changes made will become effective as soon as the dialog is
// OK'd.  Note that these settings will only apply to one session unless they
// are saved using the MGLSC_saveCustomKeyboard API call.


// HERE'S HOW YOU COMMUNICATE WITH THIS INTERFACE...
// =================================================
// Simply call MGLSC_init with the appropriate parameters.
// You can blit by posting a WM_BLITFRAME to the client window handle.
// This will blit the entire frame and update the palette if needed.
// The semaphore "framesync_sem" will be posted when the blit is complete.
// If you wish to update the palette, use the MGLSC_setColor API call and
//  then send a WM_BLITFRAME message to the client window.
// When the user specifies that they wish to go full screen or come back,
//  simply post a WM_TOGGLEFS to the client window handle.  If this is the
//  first time you've gone full screen, MGLServer will be initialized without
//  special intervention required by the developer.
// To receive user input, periodically call MGLSC_flushUserInput.
//  You should probably call this function with every frame (whether you blit
//  the frame or skip blitting it).  This function will also ensure that mouse
//  and joystick input is acknowledged.
// To capture the video buffer of your application to the clipboard, simply
//  post a WM_CLIPCAPTURE to MGLSC_clientState()->clientwin.  You will hear
//  a WA_WARNING if it succeeded or a WA_ERROR if it failed to capture.
// The WM_MOUSE_RECENTER message is used internally when mouse grabbing is
//  active.
// To adjust the window size and position to convenient values, use the
//  WM_SETWINSIZE message.  MPARAM1 is the scaling factor and MPARAM2 is
//  a flag specifying whether or not you want the window centered on the
//  desktop.  MPARAM1 is made up of two USHORTs.  The first is the X scale
//  factor and the second is the Y scale factor.  They are both scaled by
//  a factor of 1000.  So to set a 1:1 ratio, use MPFROM2SHORT( 1000, 1000 ).
// To shut down both the PM and MGL sessions cleanly, issue a WM_CLOSE
//  message to the client window handle and do a DosWaitThread on the
//  MGLSC_clientState()->pm_thread.  Or you can simply use the MGLSC_shutdown
//  API call.
//
// **IMPORTANT NOTE**
// When the DIVE blitter does not support an input source color depth, a
// custom DIVE blitter will be automatically chosen by the DualMode library.
// These custom blitters are currently still under development.
// If you have any suggestions to speed them up, I'd love to hear them.
// They were put in place to fix crashes on some systems which have spotty
// DIVE support, so it's better than nothing.

#define WM_BLITFRAME      WM_USER
#define WM_TOGGLEFS       WM_USER+1
#define WM_CLIPCAPTURE    WM_USER+2
#define WM_MOUSE_RECENTER WM_USER+3
#define WM_SETWINSIZE     WM_USER+4


// This interface reserves WM_USER through WM_USER+50, so the first real user
//  message starts at WM_USER+50 or WM_USER_MSG as I've defined below.
#define WM_USER_MSG      WM_USER+50

// Some folks might not have these definitions
#ifndef WM_VRNENABLE
#define WM_VRNENABLE 0x7f
#endif
#ifndef WM_VRNDISABLE
#define WM_VRNDISABLE 0x7e
#endif

#ifdef __cplusplus
}
#endif

#endif


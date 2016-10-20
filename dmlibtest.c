#define INCL_DOSPROCESS
#include "dualmode.h"

struct vidmode_descrip allowed_modes[] = {
  { 640, 480, 8 },
  { 640, 480, 16 },
  { 640, 480, 24 },
  { 640, 480, 32 },
  { 800, 600, 8 },
  { 800, 600, 16 },
  { 800, 600, 24 },
  { 800, 600, 32 },
  { 1024, 768, 8 },
  { 1024, 768, 16 },
  { 1024, 768, 24 },
  { 1024, 768, 32 },
  { 0, 0, 0 }
};

unsigned long *vidbuf32;

int windowClosed = 0;

void windowClose( HWND win )
{
  windowClosed = 1;
  MGLSC_saveCustomKeyboard( "DMLIBTEST.INI", "Keyboard settings",
   "Customizations" );
}

void windowOpen( HWND win )
{
  MGLSC_setMouseModeRelative( 0 );
  MGLSC_mouseGrab( 1 );
  // MGLSC_mouseGrab needs a client window handle to work correctly, so call
  //  it here, after we're sure one has been established.
  MGLSC_loadCustomKeyboard( "DMLIBTEST.INI", "Keyboard settings",
   "Customizations" );
}

void handleInput( InputEventType event, short d1, short d2 )
{
  static int oldx = -1, oldy = -1;
  static ULONG oldCol = 0, butStat = 0;
  switch( event )
  {
    case MGLSC_MOUSE_MOVE:
      if ( oldx >= 0 )
      {
        if ( !butStat )
          vidbuf32[ oldx + (oldy * 640) ] = oldCol;
        else
          vidbuf32[ oldx + (oldy * 640) ] = 0xffffffff;
      }
      oldx = d1;
      oldy = d2;
      oldCol = vidbuf32[ oldx + (oldy * 640) ];
      vidbuf32[ oldx + (oldy * 640) ] = 0xffffffff;
    break;
    case MGLSC_MOUSE_BUTTON_PRESS:
      butStat |= d1;
    break;
    case MGLSC_MOUSE_BUTTON_RELEASE:
      butStat &= ~d1;
    break;
    case MGLSC_KEYBOARD_MAKE:
      if ( d1 == 1 )
      {
        // Scancode for ESC
        WinPostMsg( MGLSC_clientState()->framewin, WM_CLOSE, 0, 0 );
      } else if ( d1 == 2 )
      {
        // Scancode for 1
        MGLSC_customizeKeyboard();
      } else {
        WinPostMsg( MGLSC_clientState()->framewin, WM_TOGGLEFS, 0, 0 );
      }
    break;
  }
}

int main( void )
{
  int i, j;

  struct MGLSC_event_handlers eventhandler =
  {
    NULL,
    // Additional window procedure
    windowOpen,
    // Window initializer
    handleInput,
    // Input event handler
    windowClose,
    // Window closed event handler
    NULL,
    // PM menu action processing
  };

  MGLSC_init( 640, 480, 32, FORMAT_DIRECTMAP | FORMAT_RGB,
   0, 0, 639, 479, &eventhandler,
   "Dual-Mode Library Test", "Dual-Mode Library Test", allowed_modes,
   MGLSC_WINDOW_DECOR_TASKLIST | MGLSC_WINDOW_DECOR_MINMAX |
   MGLSC_WINDOW_DECOR_SYSMENU  | MGLSC_WINDOW_DECOR_SYSMENU |
   MGLSC_WINDOW_DECOR_TITLEBAR, 1, 0 );

  vidbuf32 = MGLSC_clientState()->vidbuffer;

  for ( i=0; i<480; ++i )
  {
    for ( j=0; j<640; ++j )
    {
      vidbuf32[ (i*640)+j ] = ((i*255) / 480) |
       ((((640-j)*255) / 640) << 16);
    }
  }

  MGLSC_setTimingMode( 0 );
  MGLSC_setFrameRate( 30 );
  MGLSC_setMaxFrameSkip( 0 );
  
  WinPostMsg( MGLSC_clientState()->clientwin, WM_BLITFRAME, 0, 0 );
  
  while ( !windowClosed )
  {
    MGLSC_flushUserInput();
    MGLSC_blockUntilFrameReady();
    MGLSC_advanceFrame( 0 );
    WinPostMsg( MGLSC_clientState()->clientwin, WM_BLITFRAME, 0, 0 );
  }
  
  MGLSC_setFrameRate( 0 );
  MGLSC_waitForShutdown();

  return 0;
}


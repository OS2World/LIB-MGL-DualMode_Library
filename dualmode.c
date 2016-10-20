#define INCL_WIN
#define INCL_DOS
#define INCL_GPIBITMAPS
#define INCL_DOSQUEUES
#define INCL_DOSSEMAPHORES
#define INCL_DOSERRORS
#define INCL_DOSDEVIOCTL
#include <os2.h>
#include <tmr0_ioc.h>

#include <string.h>

#include "mouseregion.h"
#include "dive.h"
#include "mglserver_internal.h"
#include "controls.h"
#include "blitters.h"
#include "resource.h"

#define TIMEOUT 5 // seconds before giving up on the MGL server daemon

#define MGLSC_WAKEUP_SEM_NAME     (PUCHAR)"\\SEM32\\MGLS_PM_WAKEUP_"
#define MGLSC_LISTENER_NAME       (PUCHAR)"\\QUEUES\\MGLS_CLIENT_LISTENER_"
#define MGLSC_LISTENER_READY_NAME (PUCHAR)"\\SEM32\\MGLS_CLIENT_LISTENER_READY_"

#define FOURCC_BGR4 0x34524742ul
#define FOURCC_BGR3 0x33524742ul
#define FOURCC_RGB4 0x34424752ul
#define FOURCC_RGB3 0x33424752ul
#define FOURCC_R565 0x35363552ul
#define FOURCC_R555 0x35353552ul
#define FOURCC_LUT8 0x3854554cul
#define FOURCC_LT12 0x3231454cul
#define FOURCC_SCRN 0

//
// Disable the following #define to force the use of custom blitters.
// This is useful for debugging the custom blitters.
//
#define ALLOW_INTERNAL_DIVE_BLITTER

//
// Enable the following #define to report whenever a programmed key sequence is
// being executed if logging is enabled.
//
// #define MGLSC_DEBUG_PROGKEY

struct MGLSC_state          mglclient;
struct MGLSC_event_handlers mglevents;

char MGLup = 0, shuttingDown = 0, mouseGrab = 0, mouseRel = 1;
int PMCenterX, PMCenterY;
char mouseMoved = 0, mouseUngrabbedByFocus = 0;
int view_x1, view_y1, view_x2, view_y2;
int xscale, yscale;
HMTX reinit_mutex = 0, sync_command_mux = 0;

ULONG pollstarttime, polltime, pollbase;
double polladder, pollleftovers;
// Polling parameters

extern void RemapOffsets( int region );
// in blitters.c

void *linebuffer = NULL;
HMTX offsetmapmux = 0;
int validRegions = 0;
// Used for custom blitters

struct BlitterTypes blitters[] =
{
  { "LUT8", "LUT8", 8, 8, 0,   DirectBlit8To8 },
  { "LUT8", "R565", 8, 16, 0,  DirectBlit8To16 },
  { "LUT8", "RGB3", 8, 24, 0,  DirectBlit8To24 },
  { "LUT8", "BGR3", 8, 24, 0,  DirectBlit8To24B },
  { "LUT8", "RGB4", 8, 32, 0,  DirectBlit8To32 },
  { "LUT8", "BGR4", 8, 32, 0,  DirectBlit8To32B },
  { "R565", "LUT8", 16, 8, 0,  DirectBlit16To8 },
  { "R565", "R565", 16, 16, 0, DirectBlit16To16 },
  { "R565", "RGB3", 16, 24, 0, DirectBlit16To24 },
  { "R565", "BGR3", 16, 24, 0, DirectBlit16To24B },
  { "R565", "RGB4", 16, 32, 0, DirectBlit16To32 },
  { "R565", "BGR4", 16, 32, 0, DirectBlit16To32B },
  { "RGB3", "LUT8", 24, 8, 0,  DirectBlit24To8 },
  { "RGB3", "R565", 24, 16, 0, DirectBlit24To16 },
  { "RGB3", "RGB3", 24, 24, 0, DirectBlit24To24 },
  { "RGB3", "BGR3", 24, 24, 0, DirectBlit24To24B },
  { "RGB3", "RGB4", 24, 32, 0, DirectBlit24To32 },
  { "RGB3", "BGR4", 24, 32, 0, DirectBlit24To32B },
  { "RGB4", "LUT8", 32, 8, 0,  DirectBlit32To8 },
  { "RGB4", "R565", 32, 16, 0, DirectBlit32To16 },
  { "RGB4", "RGB3", 32, 24, 0, DirectBlit32To24 },
  { "RGB4", "BGR3", 32, 24, 0, DirectBlit32To24B },
  { "RGB4", "RGB4", 32, 32, 0, DirectBlit32To32 },
  { "RGB4", "BGR4", 32, 32, 0, DirectBlit32To32B },
  { "BGR3", "LUT8", 24, 8, 0,  DirectBlit24To8 },
  { "BGR3", "R565", 24, 16, 0, DirectBlit24BTo16 },
  { "BGR3", "RGB3", 24, 24, 0, DirectBlit24To24B },
  { "BGR3", "BGR3", 24, 24, 0, DirectBlit24To24 },
  { "BGR3", "RGB4", 24, 32, 0, DirectBlit24To32B },
  { "BGR3", "BGR4", 24, 32, 0, DirectBlit24To32 },
  { "BGR4", "LUT8", 32, 8, 0,  DirectBlit32To8 },
  { "BGR4", "R565", 32, 16, 0, DirectBlit32BTo16 },
  { "BGR4", "RGB3", 32, 24, 0, DirectBlit32To24B },
  { "BGR4", "BGR3", 32, 24, 0, DirectBlit32To24 },
  { "BGR4", "RGB4", 32, 32, 0, DirectBlit32To32B },
  { "BGR4", "BGR4", 32, 32, 0, DirectBlit32To32 },
  { "\0\0\0\0", "\0\0\0\0", 0, 0, 0, NULL }
};

typedef struct
{
  int numInstructions;
  unsigned short *instructions;
} programmableKey;

int numregions = 0;
blitterRegion *regions = NULL;
programmableKey keyDefs[ 106 ] = { {0, NULL} };

#define KEYDEF_PRESS         0
#define KEYDEF_RELEASE       1
#define KEYDEF_REPEAT        2
#define KEYDEF_WAIT          3
#define KEYDEF_INSTR( inst ) ((inst) & 0xff)
#define KEYDEF_PARAM( inst ) (((inst) >> 8) & 0xff)
#define KEYDEF_MAKEINSTR( inst, param ) ((inst & 0xff) | ((param & 0xff) << 8))

void (*DiveBlitRoutine) ( int ) = NULL;

void UseDiveBlitter( int junk )
{
  if ( mglclient.palette_dirty )
  {
    MGL_SERVER_COLORS_SET_PACKET *cs = 
     (MGL_SERVER_COLORS_SET_PACKET *) mglclient.shared_packet;

    DiveSetSourcePalette( mglclient.diveinst, 0, 256, (PBYTE)cs->colors );
  }

  DiveBlitImage( mglclient.diveinst, mglclient.divebufnum,
   DIVE_BUFFER_SCREEN );
}

int MGLSC_PMkeyToMGLKeycode( int keycode )
{
  return PMkeys[ keycode ].MGL_key;
}

int MGLSC_MGLkeyToPMKeycode( int keycode )
{
  int i;

  for ( i=0; i<DEFINED_KEYS; ++i )
  {
    if ( PMkeys[ i ].MGL_key == keycode )
    {
      return i;
    }
  }
  return 0; // Illegal scan code in both MGL and PM
}

const char * MGLSC_getKeyName( int PMkeycode )
{
  return PMkeys[ PMkeycode ].description;
}

struct MGLSC_state *MGLSC_clientState( void )
{
  return &mglclient;
}

void MGLSC_flushUserInput( void )
{
  if ( !mglclient.isFullScreen )
  {
    if ( mglevents.ProcessInput && ((mouseRel && mouseGrab) || !mouseRel)
          && mouseMoved )
    {
      POINTL ppos;
      int tmpCurX;
      int tmpCurY;

      WinQueryPointerPos( HWND_DESKTOP, &ppos );

      // Make sure PM mouse input is acted on too

      if ( mouseRel )
      {
        tmpCurX = ppos.x - PMCenterX;
        tmpCurY = PMCenterY - ppos.y;
        // "Up" increases row in PM

        if ( !tmpCurX && !tmpCurY )
        {
          mouseMoved = 0;
          return;
        }
        // No real movement happened.

        if ( tmpCurX >  mglclient.width/2 )  tmpCurX =   mglclient.width/2;
        if ( tmpCurX < -mglclient.width/2 )  tmpCurX = -(mglclient.width/2);
        if ( tmpCurY >  mglclient.height/2 ) tmpCurY =   mglclient.height/2;
        if ( tmpCurY < -mglclient.height/2 ) tmpCurY = -(mglclient.height/2);

        mglevents.ProcessInput( MGLSC_MOUSE_MOVE, tmpCurX, tmpCurY );
        WinPostMsg( mglclient.clientwin, WM_MOUSE_RECENTER, 0, 0 );
      } else {
        int mouseAbsX, mouseAbsY;
        
        if ( mouseGrab )
        {
          // Mouse is constrained to the size of the viewport in pixels and
          //  is 0-based, so coordinates are just a direct read.
          mouseAbsX = ppos.x;
          mouseAbsY = mglclient.desktopheight - ppos.y;
        } else {
          // Mouse could be anywhere on the desktop.  If it's inside our
          //  window, figure out where.  If not, move it to the closest edge
          //  of our window.  Note that the coordinates are scaled to the
          //  viewport size (not the window size).
          mouseAbsX = (ppos.x - mglclient.winposx) *
           mglclient.width / mglclient.winsizex;
          mouseAbsY = ((mglclient.desktopheight - ppos.y) -
           mglclient.winposy) * mglclient.height / mglclient.winsizey;
        }
        
        if ( mouseAbsX >= mglclient.width ) mouseAbsX = mglclient.width - 1;
        if ( mouseAbsX < 0 ) mouseAbsX = 0;
        if ( mouseAbsY >= mglclient.height ) mouseAbsY = mglclient.height - 1;
        if ( mouseAbsY < 0 ) mouseAbsY = 0;
        
        mglevents.ProcessInput( MGLSC_MOUSE_MOVE, mouseAbsX, mouseAbsY );
      }
      mouseMoved = 0;
    }

    return;
  }

  DosWriteQueue( mglclient.command_queue, MGLS_EVENT_FLUSH, 0, NULL, 0 );
  if ( hEJoy && mglevents.ProcessInput )
  {
    int i;

    // update joystick input too

    for ( i=0; i<num_joysticks; ++i )
    {
      ejoy_ext_get_info( i );
      if ( !i )
      {
        mglevents.ProcessInput( MGLSC_JOYSTICK_MOVE, ejoy_info.xpos, ejoy_info.ypos );
        mglevents.ProcessInput( MGLSC_JOYSTICK_BUTTON, ejoy_info.buttons, 0 );
      } else {
        mglevents.ProcessInput( MGLSC_JOYSTICK2_MOVE, ejoy_info.xpos, ejoy_info.ypos );
        mglevents.ProcessInput( MGLSC_JOYSTICK2_BUTTON, ejoy_info.buttons, 0 );
      }
    }
  }
}

void MGLS_synchronous_command( int command, int packetsize, void *packet )
{
  ULONG junk;

  DosRequestMutexSem( sync_command_mux, SEM_INDEFINITE_WAIT );
  DosResetEventSem( mglclient.client_wakeup, &junk );
  if ( !mglclient.isFullScreen ) return;
  junk = DosWriteQueue( mglclient.command_queue, command, packetsize, packet, 0 );
  if ( junk )
  {
    // Error writing to the queue.  MGLServer may have ended unexpectedly.
    DosSelectSession( 0 );
    DosSleep( 1000 ); // Give it a chance to switch
    mglclient.isFullScreen = 0;
    MGLup = 0;

    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "Error (%ld) writing to MGLServer command queue.\n", junk );
      fflush( mglclient.dbfp );
    #endif

    return;
  }
  DosWaitEventSem( mglclient.client_wakeup, SEM_INDEFINITE_WAIT );
  DosReleaseMutexSem( sync_command_mux );
}

static void MGLSC_MGL_reinit( void );

void ClientListenerThread( void *unused )
{
  REQUESTDATA request;
  ULONG information, rc, junk;
  BYTE priority;
  HEV queue_ready;
  int relx, rely, lastbuttons = 0;

  DosSetPriority( PRTYS_THREAD, PRTYC_TIMECRITICAL, 0, 0 );
  // Make sure keyboard is responsive

  {
    char readysemname[ 80 ] = { 0 };

    strcpy( readysemname, (char *)(MGLSC_LISTENER_READY_NAME) );
    strncat( readysemname, mglclient.winclassname, 10 );

    rc = DosCreateEventSem( readysemname, &queue_ready,
      DC_SEM_SHARED, FALSE );

    if ( rc )
    {
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "Listener thread could not establish semaphore. RC = %ld\n", rc );
        fflush( mglclient.dbfp );
      #endif

      _endthread();
      return;
    }
  }

  rc = 0;

  while ( !rc ) // While queue handle is valid
  {
    rc = DosReadQueue( mglclient.listener_queue, &request, &junk,
      (void **) &information, 0, DCWW_NOWAIT, &priority, queue_ready );

    while ( rc == ERROR_QUE_EMPTY )
    {
      if ( DosWaitEventSem( queue_ready, 500 ) == ERROR_TIMEOUT )
      {
        if ( mglclient.isFullScreen )
        {
          rc = DosWriteQueue( mglclient.command_queue, MGLS_EVENT_FLUSH, 0, NULL, 0 );
          // Keep the full screen session alive and responsive

          if ( rc ) break;  // Queue error
        }
      }
      // Makes sure that events are flushed at least twice per second while waiting
      // for information on the listener queue.

      rc = DosReadQueue( mglclient.listener_queue, &request, &junk,
        (void **) &information, 0, DCWW_NOWAIT, &priority, queue_ready );
    }

    if ( rc ) break;
    // A different queue error occurred.  Break out.

    switch ( request.ulData )
    {
      case MGLC_VIDEO_SWITCH_NOTIFICATION:
        mglclient.isFullScreen = information;

        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "Video mode switch notification received.  Mode = %ld\n", information );
          fflush( mglclient.dbfp );
        #endif

        if ( !mglclient.isFullScreen )
        {
          DosPostEventSem( mglclient.client_wakeup );
        } else {
          if ( mglclient.vidmode_reinit && !shuttingDown )
          {
            mglclient.disallow_blit = 1;
            MGLSC_MGL_reinit();
            mglclient.disallow_blit = 0;
          }
          // Video mode changed and this is the first time the MGL session
          // was reactivated, so make the change take effect now.
        }

        mglclient.palette_dirty = 1;

        if ( mglclient.autoBlitVidBufferOnRefresh )
        {
          WinPostMsg( mglclient.clientwin, WM_BLITFRAME, 0, 0 );
        }
      break;
      case MGLC_KEYDOWN_NOTIFICATION:
        if ( keyDefs[ information ].numInstructions )
        {
          if ( mglclient.activeKeyDef != information &&
               information <= 105 )
          {
            mglclient.activeKeyDef = information;
            mglclient.keyDefInstruction = 0;
            mglclient.keyDefWaitCount = 0;
            #ifdef MGLSC_DEBUG_LOG
            #ifdef MGLSC_DEBUG_PROGKEY
              fprintf( mglclient.dbfp, "KEYDEF START: %d\n",
               mglclient.activeKeyDef );
              fflush( mglclient.dbfp );
            #endif
            #endif
          }
        } else {
          if ( mglevents.ProcessInput )
            mglevents.ProcessInput( MGLSC_KEYBOARD_MAKE, information, 0 );
        }
      break;
      case MGLC_KEYUP_NOTIFICATION:
        if ( mglclient.activeKeyDef == information )
        {
          if ( KEYDEF_INSTR( keyDefs[mglclient.activeKeyDef].instructions[
                keyDefs[mglclient.activeKeyDef].numInstructions - 1 ] ) ==
                KEYDEF_REPEAT )
          {
            // Repeated key defs must be held down to keep working.
            
            #ifdef MGLSC_DEBUG_LOG
            #ifdef MGLSC_DEBUG_PROGKEY
              fprintf( mglclient.dbfp, "KEYDEF END: %d\n",
               mglclient.activeKeyDef );
              fflush( mglclient.dbfp );
            #endif
            #endif
            mglclient.activeKeyDef = 0;
            mglclient.keyDefInstruction = 0;
            mglclient.keyDefWaitCount = 0;
          }
        } else {
          if ( mglevents.ProcessInput )
            mglevents.ProcessInput( MGLSC_KEYBOARD_BREAK, information, 0 );
        }
      break;
      case MGLC_MOUSEMOVE_NOTIFICATION:
        if ( mglevents.ProcessInput )
        {
          if ( mouseRel )
          {
            relx = (information & 0xffff) - (mglclient.fswidth/2);
            rely = ((information >> 16) & 0xffff) - (mglclient.fsheight/2);
            // MGLServer returns absolute coordinates, so relativize them here

            if ( relx || rely )
            {
              mglevents.ProcessInput( MGLSC_MOUSE_MOVE, relx, rely );

              WinPostMsg( mglclient.clientwin, WM_MOUSE_RECENTER, 0, 0 );
            }
          } else {
            mglevents.ProcessInput( MGLSC_MOUSE_MOVE, information & 0xffff,
             (information >> 16) & 0xffff );
          }
        }
      break;
      case MGLC_MOUSEBUTTON_NOTIFICATION:
      {
        int makebut = 0, breakbut = 0;
        if ( (lastbuttons&1) && !(information&1) ) breakbut |= 1;
        else if ( !(lastbuttons&1) && (information&1) ) makebut |= 1;
        if ( (lastbuttons&2) && !(information&2) ) breakbut |= 2;
        else if ( !(lastbuttons&2) && (information&2) ) makebut |= 2;
        if ( (lastbuttons&4) && !(information&4) ) breakbut |= 4;
        else if ( !(lastbuttons&4) && (information&4) ) makebut |= 4;

        lastbuttons = information;

        if ( makebut )
        {
          if ( mglevents.ProcessInput )
            mglevents.ProcessInput( MGLSC_MOUSE_BUTTON_PRESS, makebut, 0 );
        }
        if ( breakbut )
        {
          if ( mglevents.ProcessInput )
            mglevents.ProcessInput( MGLSC_MOUSE_BUTTON_RELEASE, breakbut, 0 );
        }
      }
      break;
    }
  }

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Client listener queue has shut down.  DosReadQueue rc=%ld.\n", rc );
    fflush( mglclient.dbfp );
  #endif

  DosPostEventSem( mglclient.client_wakeup );
  // Tell the main thread that we're done.

  DosCloseEventSem( queue_ready );

  _endthread();
}

int MGLSC_MGL_init( void )
{
  ULONG rc, i;
  STARTDATA sd = {0};
  MGL_SERVER_INIT_PACKET *ip;
  MGL_SERVER_INIT_VIDMODE_PACKET *iv;
  MGL_SERVER_INIT_BUFFER_PACKET *ib;
  STATUSDATA statdat;

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Create wakeup sem\n" );
    fflush( mglclient.dbfp );
  #endif

  ip = (MGL_SERVER_INIT_PACKET *) mglclient.shared_packet;
  strcpy( ip->client_semaphore_name, (char *)(MGLSC_WAKEUP_SEM_NAME) );
  strncat( ip->client_semaphore_name, mglclient.winclassname, 10 );
  strcpy( ip->input_queue_name, (char *)(MGLSC_LISTENER_NAME) );
  strncat( ip->input_queue_name, mglclient.winclassname, 10 );
  // Personalize the semaphore and queue names

  rc = DosCreateEventSem( ip->client_semaphore_name, 
    &(mglclient.client_wakeup), DC_SEM_SHARED, FALSE );
  rc |= DosCreateMutexSem( NULL, &sync_command_mux, 0, FALSE );

  if ( rc )
  {
    return 1;
  }

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Check if server is already running\n" );
    fflush( mglclient.dbfp );
  #endif

  rc = DosOpenQueue( &(mglclient.server_pid), &(mglclient.command_queue),
    ip->input_queue_name );

  if ( !rc )
  {
		DosCloseQueue( mglclient.command_queue );
    DosCloseEventSem( mglclient.client_wakeup );
    return 2;
  }
  // Daemon is already running and is probably owned by someone else.
  // Don't start up.

  if ( rc != 343 )
  {
    return 3;
  }
  // DosOpenQueue failed for some other wacky reason.

  sd.Length = 32;
  sd.Related = 1;
  sd.FgBg = SSF_FGBG_FORE;
  sd.TraceOpt = SSF_TRACEOPT_NONE;
  sd.PgmTitle = NULL;
  sd.PgmName = (PUCHAR)"mglserver.exe";
  sd.PgmInputs = NULL;
  sd.TermQ = NULL;
  sd.Environment = 0;
  sd.InheritOpt = SSF_INHERTOPT_PARENT;
  sd.SessionType = SSF_TYPE_FULLSCREEN;

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Start up new server\n" );
    fflush( mglclient.dbfp );
  #endif

  rc = DosStartSession( &sd, &(mglclient.MGL_session), &mglclient.server_pid );

  if ( rc ) { 
    DosCloseQueue( mglclient.command_queue );
    DosCloseEventSem( mglclient.client_wakeup );
    return 4;
  }

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Set session properties\n" );
    fflush( mglclient.dbfp );
  #endif

  statdat.Length = sizeof( USHORT ) * 3;
  statdat.SelectInd = SET_SESSION_NON_SELECTABLE;
  statdat.BondInd = 0;
  DosSetSession( mglclient.MGL_session, &statdat );
  // Take the ugly "MGLSERVER.EXE" out of the "Switch To" list

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Wait for command queue to exist\n" );
    fflush( mglclient.dbfp );
  #endif

  for (i=0; i<TIMEOUT; ++i)
  {
    DosSleep(1000);
    rc = DosOpenQueue( &(mglclient.server_pid), &(mglclient.command_queue),
      MGLS_COMMAND_QUEUE_NAME );
    if ( !rc ) break;
  }

  if ( rc )
  {
    DosCloseEventSem( mglclient.client_wakeup );
    return 6;
  }

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Create listener queue\n" );
    fflush( mglclient.dbfp );
  #endif

  rc = DosCreateQueue( &(mglclient.listener_queue), QUE_FIFO, ip->input_queue_name );
  if ( rc )
  {
    DosCloseQueue( mglclient.command_queue );
    DosCloseEventSem( mglclient.client_wakeup );
    return 7;
  }

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Launch listener thread\n" );
    fflush( mglclient.dbfp );
  #endif

  mglclient.listener_thread = _beginthread( 
    ClientListenerThread, NULL, 16384, NULL );

  // Fire off the listener thread to get feedback from the
  // daemon.  Needed for mode switch notification and input
  // device notifications.

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Client listener thread (%ld) started.\n", 
      mglclient.listener_thread );
    fprintf( mglclient.dbfp, "Go full screen\n" );
    fflush( mglclient.dbfp );
  #endif

  rc = DosSelectSession( mglclient.MGL_session );
  if ( rc )
  {
    DosKillThread( mglclient.listener_thread );
    DosCloseQueue( mglclient.command_queue );
    DosCloseQueue( mglclient.listener_queue );
    DosCloseEventSem( mglclient.client_wakeup );
    return 8;
  }

  mglclient.isFullScreen = 1;

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Init server\n" );
    fflush( mglclient.dbfp );
  #endif

  MGLS_synchronous_command( MGLS_INIT, sizeof(MGL_SERVER_INIT_PACKET), ip );

  iv = (MGL_SERVER_INIT_VIDMODE_PACKET *) mglclient.shared_packet;
  iv->flags = 0;

  for ( i=0; mglclient.allowedmodes[i].width; ++i )
  {
    if ( mglclient.allowedmodes[i].width <= view_x2 - view_x1 ) continue;
    if ( mglclient.allowedmodes[i].height <= view_y2 - view_y1 ) continue;
    if ( mglclient.allowedmodes[i].depth < mglclient.depth ) continue;

    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "Attempting FS mode %d x %d %dbpp.\n", 
        mglclient.allowedmodes[i].width, mglclient.allowedmodes[i].height,
        mglclient.allowedmodes[i].depth );
      fflush( mglclient.dbfp );
    #endif

    iv->width = mglclient.allowedmodes[i].width;
    iv->height = mglclient.allowedmodes[i].height;
    iv->depth = mglclient.allowedmodes[i].depth;
    iv->flags = (mglclient.custommodes ? MGLS_VMIFLAG_USE_CUSTOM : 0);

    MGLS_synchronous_command( MGLS_INIT_VIDMODE, 
      sizeof(MGL_SERVER_INIT_VIDMODE_PACKET), iv );

    if ( iv->flags & MGLS_VMOFLAG_SUCCESS ) break;
  }

  if ( !(iv->flags & MGLS_VMOFLAG_SUCCESS) )
  {
    MGLS_synchronous_command( MGLS_SHUTDOWN,
      sizeof(MGL_SERVER_SHUTDOWN_PACKET), mglclient.shared_packet );

    DosSelectSession( 0 );

    DosKillThread( mglclient.listener_thread );
    DosCloseQueue( mglclient.command_queue );
    DosCloseQueue( mglclient.listener_queue );

    DosCloseEventSem( mglclient.client_wakeup );
    return 9;
    // Could not use any acceptable modes
  }

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Success using FS mode %d x %d %dbpp.\n", 
      mglclient.allowedmodes[i].width, mglclient.allowedmodes[i].height,
      mglclient.allowedmodes[i].depth );
    fflush( mglclient.dbfp );
  #endif

  mglclient.fswidth  = mglclient.allowedmodes[i].width;
  mglclient.fsheight = mglclient.allowedmodes[i].height;
  mglclient.fsdepth  = mglclient.allowedmodes[i].depth;

  ib = (MGL_SERVER_INIT_BUFFER_PACKET *) mglclient.shared_packet;
  ib->buffer = mglclient.vidbuffer;
  ib->width = mglclient.width;
  ib->height = mglclient.height;
  ib->depth = mglclient.depth;

  MGLS_synchronous_command( MGLS_INIT_BUFFER, 
    sizeof(MGL_SERVER_INIT_BUFFER_PACKET), ib );
  // MGL now has a memory device context associated with this buffer.
  // We're ready to blit!

  mglclient.autoBlitVidBufferOnRefresh = 1;
  if ( mglclient.depth == 8 )
  {
    mglclient.palette_dirty = 1;
    DosWriteQueue( mglclient.command_queue, MGLS_SET_COLORS,
     sizeof(MGL_SERVER_COLORS_SET_PACKET), mglclient.shared_packet, 0 );
  }

  if ( DosCreateMutexSem( NULL, &reinit_mutex, 0, FALSE ) )
  {
    MGLS_synchronous_command( MGLS_FREE_BUFFER,
      sizeof(MGL_SERVER_FREE_BUFFER_PACKET), mglclient.shared_packet );
    MGLS_synchronous_command( MGLS_SHUTDOWN_VIDMODE,
      sizeof(MGL_SERVER_SHUTDOWN_VIDMODE_PACKET), mglclient.shared_packet );
    MGLS_synchronous_command( MGLS_SHUTDOWN,
      sizeof(MGL_SERVER_SHUTDOWN_PACKET), mglclient.shared_packet );

    DosSelectSession( 0 );

    DosKillThread( mglclient.listener_thread );
    DosCloseQueue( mglclient.command_queue );
    DosCloseQueue( mglclient.listener_queue );

    DosCloseEventSem( mglclient.client_wakeup );
    return 10;
    // Couldn't create mutex semaphore
  }

  return 0;
}

void MGLS_shutdown( void )
{
  DosSelectSession( mglclient.MGL_session );

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Telling MGLServer to free buffer.\n" );
    fflush( mglclient.dbfp );
  #endif

  MGLS_synchronous_command( MGLS_FREE_BUFFER,
    sizeof(MGL_SERVER_FREE_BUFFER_PACKET), mglclient.shared_packet );

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Telling MGLServer to shut down video mode.\n" );
    fflush( mglclient.dbfp );
  #endif

  MGLS_synchronous_command( MGLS_SHUTDOWN_VIDMODE,
    sizeof(MGL_SERVER_SHUTDOWN_VIDMODE_PACKET), mglclient.shared_packet );

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Telling MGLServer itself to shut down.\n" );
    fflush( mglclient.dbfp );
  #endif

  MGLS_synchronous_command( MGLS_SHUTDOWN,
    sizeof(MGL_SERVER_SHUTDOWN_PACKET), mglclient.shared_packet );

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Free up video buffer and shared packet storage.\n" );
    fflush( mglclient.dbfp );
  #endif

  DosFreeMem( mglclient.shared_packet );
  DosFreeMem( mglclient.vidbuffer );

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Close command and listener queues.\n" );
    fflush( mglclient.dbfp );
  #endif

  DosCloseQueue( mglclient.command_queue );
  DosCloseQueue( mglclient.listener_queue );

  DosCloseEventSem( mglclient.client_wakeup );
  DosCloseMutexSem( sync_command_mux );
  sync_command_mux = 0;

  DosSleep( 1000 );
  // Take a breather

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "MGLServer shutdown succeeded.\n" );
    fflush( mglclient.dbfp );
  #endif
}

void ReportDIVECaps( int which )
{
  DIVE_CAPS cap;
  char buffer[512];
  int i, j;
  unsigned char *str;

  cap.ulStructLen = sizeof( DIVE_CAPS );
  cap.ulFormatLength = 512;
  cap.pFormatData = buffer;
  cap.ulPlaneCount = 0;
  DiveQueryCaps( &cap, which );

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Dive capabilities-\n=> Number of planes: %ld\n=> Screen direct access: %ld\n=> Bank switched operations required: %ld\n=> Bits per pixel: %ld\n=> Screen width: %ld\n=> Screen height: %ld\n=> Screen scan line length: %ld\n=> Color encoding: %c%c%c%c\n=> VRAM aperture size: %ld\n",
     cap.ulPlaneCount, cap.fScreenDirect, cap.fBankSwitched, cap.ulDepth,
     cap.ulHorizontalResolution, cap.ulVerticalResolution, cap.ulScanLineBytes,
     (unsigned char) cap.fccColorEncoding&0xff,
     (unsigned char) (cap.fccColorEncoding>>8)&0xff,
     (unsigned char) (cap.fccColorEncoding>>16)&0xff,
     (unsigned char) (cap.fccColorEncoding>>24)&0xff,
     cap.ulApertureSize );
  #endif

  mglclient.desktopdepth = cap.ulDepth;
  mglclient.desktopscan = cap.ulScanLineBytes / (cap.ulDepth>>3);
  mglclient.desktopheight = cap.ulVerticalResolution;
  mglclient.desktopFourCC = cap.fccColorEncoding;

  linebuffer = (void *) malloc( cap.ulScanLineBytes );
  // Set up these buffers now, since it's a one-shot deal and we know
  //  everything we need here.

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "=> Input formats: " );
  #endif

  for ( i=0; i<cap.ulInputFormats; ++i )
  {
    str = cap.pFormatData + (i*4);
    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "%c%c%c%c ", str[0], str[1], str[2], str[3] );
    #endif

    for ( j=0; blitters[j].depth; ++j )
    {
      if ( *((ULONG *)str) == *((ULONG *)blitters[j].srcFourCC) )
      {
        blitters[j].supportedByDIVE = 1;
      }
    }
  }

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "\n=> Output formats: " );
  #endif

  for ( i=0; i<cap.ulOutputFormats; ++i )
  {
    str = cap.pFormatData + (i*4);
    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "%c%c%c%c ", str[0], str[1], str[2], str[3] );
    #endif

    for ( j=0; blitters[j].depth; ++j )
    {
      if ( !blitters[j].supportedByDIVE ) continue;
      
      if ( *((ULONG *)blitters[j].destFourCC) != *((ULONG *)str) &&
           *((ULONG *)blitters[j].destFourCC) != mglclient.desktopFourCC )
      {
        blitters[j].supportedByDIVE = 0;
      }
    }
  }

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "\n\n" );
    fflush( mglclient.dbfp );
  #endif

  if ( cap.fScreenDirect && !cap.fBankSwitched )
  {
    mglclient.allowcustomblitter = 1;
  }
}

MRESULT EXPENTRY DualModePM(HWND win, ULONG message, MPARAM mp1, MPARAM mp2)
{
  static ULONG err;
  static HPS hps;
  static HRGN hrgn;
  static RGNRECT rgnCtl;
  static RECTL *rcls, rectl;
  static int i, numrcls = 0;
  static SWP swp;
  static POINTL pointl;

  switch ( message )
  {
    case WM_CREATE:
    {
      MGLup = 0;
      validRegions = 0;
      mglclient.autoBlitVidBufferOnRefresh = 1;
      
      rcls = (RECTL *) malloc( 100 * sizeof( RECTL ) );
      numrcls = 100;

      WinQueryWindowRect( HWND_DESKTOP, &rectl );
      PMCenterX = (rectl.xRight - rectl.xLeft) / 2;
      PMCenterY = (rectl.yTop - rectl.yBottom) / 2;

      err = DiveOpen( &(mglclient.diveinst), FALSE,
       (PVOID)&mglclient.PMframebuffer );

      if ( err )
      {
        return MRFROMSHORT(-1);
      }

      if ( DiveBlitRoutine != UseDiveBlitter )
      {
        mglclient.disallow_blit = 0;
        break;
      }
      // Done with DIVE init here if we're not using the DIVE blitter

      err = DiveAllocImageBuffer( mglclient.diveinst, 
        &(mglclient.divebufnum), mglclient.depthfourcc,
        mglclient.width, mglclient.height, 
        mglclient.width * (mglclient.depth / 8), 
        (PBYTE *) mglclient.vidbuffer );

      if ( err )
      {
        DiveClose( mglclient.diveinst );
        mglclient.diveinst = 0;
        return MRFROMSHORT(-1);
      }

      mglclient.disallow_blit = 0;
      break;
    }
    case WM_BLITFRAME:
    {
      if ( mglclient.disallow_blit ) return 0;
      if ( mglclient.isFullScreen )
      {
        MGL_SERVER_BLIT_BUFFER_PACKET *bb = 
          (MGL_SERVER_BLIT_BUFFER_PACKET *)
          mglclient.shared_packet;

        if ( mglclient.palette_dirty )
        {
          MGL_SERVER_COLORS_SET_PACKET *cs = 
            (MGL_SERVER_COLORS_SET_PACKET *)
            mglclient.shared_packet;

          // Palette should already be loaded into cs->colors[]

          DosWriteQueue( mglclient.command_queue, MGLS_SET_COLORS,
            sizeof(MGL_SERVER_COLORS_SET_PACKET), cs, 0 );
          // Don't sync this call, just sync the frame blit below.
          // (no need to waste twice the time)
        }

        bb->left = view_x1;
        bb->right = view_x2 + 1;
        bb->top = view_y1;
        bb->bottom = view_y2 + 1;
        if ( (view_x2 - view_x1 + 1) < mglclient.fswidth ||
             (view_y2 - view_y1 + 1) < mglclient.fsheight )
        {
          if ( mglclient.stretchblit )
          {
            MGL_SERVER_STRETCHBLIT_BUFFER_PACKET *sb = 
              (MGL_SERVER_STRETCHBLIT_BUFFER_PACKET *)
              mglclient.shared_packet;

            sb->destleft = 0;
            sb->desttop = 0;
            sb->destright = mglclient.fswidth;
            sb->destbottom = mglclient.fsheight;

            MGLS_synchronous_command( MGLS_STRETCHBLIT_BUFFER,
              sizeof(MGL_SERVER_BLIT_BUFFER_PACKET), sb );
            // Stretch image to screen size
          } else {
            bb->destx = (mglclient.fswidth  - (view_x2 - view_x1 + 1) ) / 2;
            bb->desty = (mglclient.fsheight - (view_y2 - view_y1 + 1) ) / 2;
            // Center on the screen

            MGLS_synchronous_command( MGLS_BLIT_BUFFER,
              sizeof(MGL_SERVER_BLIT_BUFFER_PACKET), bb );
          }
        } else {
          bb->destx = 0;
          bb->desty = 0;

          MGLS_synchronous_command( MGLS_BLIT_BUFFER,
            sizeof(MGL_SERVER_BLIT_BUFFER_PACKET), bb );
        }
      } else {
        if ( DiveBlitRoutine != UseDiveBlitter )
        {
/*
          #ifdef MGLSC_DEBUG_LOG
            fprintf( mglclient.dbfp, "BLITFRAME\n" );
            fflush( mglclient.dbfp );
          #endif
*/
          DosRequestMutexSem( offsetmapmux, SEM_INDEFINITE_WAIT );
          for ( i=0; i<validRegions; ++i )
          {
            DiveBlitRoutine( i );
          }
          DosReleaseMutexSem( offsetmapmux );
/*
          #ifdef MGLSC_DEBUG_LOG
            fprintf( mglclient.dbfp, "BLITFRAME done\n" );
            fflush( mglclient.dbfp );
          #endif
*/
        } else {
          DosRequestMutexSem( offsetmapmux, SEM_INDEFINITE_WAIT );
          DiveBlitRoutine( 0 );
          DosReleaseMutexSem( offsetmapmux );
        }
      }
      mglclient.palette_dirty = 0;
      if ( mglclient.framesync_sem )
        DosPostEventSem( mglclient.framesync_sem );
      break;
    }
    case WM_CHAR:
    {
      if ( (SHORT1FROMMP( mp1 ) & KC_SCANCODE) )
      {
        if ( (SHORT1FROMMP( mp1 ) & KC_KEYUP) == 0 )
        {
          if ( keyDefs[ PMkeys[ CHAR4FROMMP(mp1) ].MGL_key ].numInstructions )
          {
            if ( mglclient.activeKeyDef != PMkeys[ CHAR4FROMMP(mp1) ].MGL_key &&
                 PMkeys[ CHAR4FROMMP(mp1) ].MGL_key <= 105 )
            {
              mglclient.activeKeyDef = PMkeys[ CHAR4FROMMP(mp1) ].MGL_key;
              mglclient.keyDefInstruction = 0;
              mglclient.keyDefWaitCount = 0;
              #ifdef MGLSC_DEBUG_LOG
              #ifdef MGLSC_DEBUG_PROGKEY
                fprintf( mglclient.dbfp, "KEYDEF START: %d\n",
                 mglclient.activeKeyDef );
              #endif
              #endif
            }
          } else {
            if ( mglevents.ProcessInput )
              mglevents.ProcessInput( MGLSC_KEYBOARD_MAKE,
               PMkeys[ CHAR4FROMMP(mp1) ].MGL_key, 0 );
          }
        } else {
          if ( mglclient.activeKeyDef == PMkeys[ CHAR4FROMMP(mp1) ].MGL_key )
          {
            if ( KEYDEF_INSTR( keyDefs[mglclient.activeKeyDef].instructions[
                  keyDefs[mglclient.activeKeyDef].numInstructions - 1 ] ) ==
                  KEYDEF_REPEAT )
            {
              // Repeated key defs must be held down to keep working.
              
              #ifdef MGLSC_DEBUG_LOG
              #ifdef MGLSC_DEBUG_PROGKEY
                fprintf( mglclient.dbfp, "KEYDEF END: %d\n",
                 mglclient.activeKeyDef );
                fflush( mglclient.dbfp );
              #endif
              #endif
              mglclient.activeKeyDef = 0;
              mglclient.keyDefInstruction = 0;
              mglclient.keyDefWaitCount = 0;
            }
          } else {
            if ( mglevents.ProcessInput )
              mglevents.ProcessInput( MGLSC_KEYBOARD_BREAK,
               PMkeys[ CHAR4FROMMP(mp1) ].MGL_key, 0 );
          }
        }
        // Some scancodes are different between MGL and PM.
        // Xlate as needed
      }
      break;
    }
    case WM_QUIT:
    case WM_CLOSE:
      if ( shuttingDown )
      {
        // If we get a WM_QUIT and a WM_CLOSE at the same time
        return WinDefWindowProc( win, message, mp1, mp2 );
      }
      shuttingDown = 1;
      
      mglclient.disallow_blit = 1;
      WinSendMsg( win, WM_VRNDISABLE, 0, 0 );

      if ( hEJoy ) ejoy_close();

      if ( mglevents.WindowClosed )
        mglevents.WindowClosed( win );

      if ( MGLup )
      {
        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "Switching to MGL session for shutdown.\n" );
          fflush( mglclient.dbfp );
        #endif

        DosSelectSession( mglclient.MGL_session );
        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "Shutting down MGL Server.\n" );
          fflush( mglclient.dbfp );
        #endif
        MGLS_shutdown();
        MGLup = 0;
      }

      shuttingDown = 0;
      
      break;
    case WM_PAINT:
/*
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "PAINT (%d regions)\n", validRegions );
        fflush( mglclient.dbfp );
      #endif
*/

      if ( !mglclient.disallow_blit )
      {
        if ( DiveBlitRoutine != UseDiveBlitter )
        {
          DosRequestMutexSem( offsetmapmux, SEM_INDEFINITE_WAIT );
          for ( i=0; i<validRegions; ++i )
          {
/*
            #ifdef MGLSC_DEBUG_LOG
              fprintf( mglclient.dbfp, "[%d]: Wx %d Wy %d (%dx%d) - (%d,%d)(%d,%d) %x %x\n",
               i, regions[i].winposx, regions[i].winposy, regions[i].winsizex,
               regions[i].winsizey, regions[i].view_x1, regions[i].view_y1,
               regions[i].view_x2, regions[i].view_y2, regions[i].offsetmapx,
               regions[i].offsetmapy );
              fflush( mglclient.dbfp );
            #endif
*/

            DiveBlitRoutine( i );
          }
          DosReleaseMutexSem( offsetmapmux );
        } else {
          DosRequestMutexSem( offsetmapmux, SEM_INDEFINITE_WAIT );
          DiveBlitRoutine( 0 );
          DosReleaseMutexSem( offsetmapmux );
        }
      }
/*
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "PAINT done\n" );
        fflush( mglclient.dbfp );
      #endif
*/
      break;
    case WM_VRNENABLE:
      if ( !mglclient.diveinst ) break;

/*
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "VRNENABLE\n" );
        fflush( mglclient.dbfp );
      #endif
*/
      hps = WinGetPS(win);
      if ( hps == 0 ) { err=1; break; }
      hrgn = GpiCreateRegion(hps, 0L, NULL);

      DosRequestMutexSem( offsetmapmux, SEM_INDEFINITE_WAIT );

      if (hrgn)
      {
        err = WinQueryVisibleRegion(win, hrgn);
        rgnCtl.ircStart = 1;
        rgnCtl.ulDirection=RECTDIR_LFRT_TOPBOT;
        rgnCtl.crc = 0;

        err = GpiQueryRegionRects(hps, hrgn, NULL, &rgnCtl, NULL );
        // Populates crcReturned with the number of rectangles needed.

        if ( !numrcls )
        {
          numrcls = rgnCtl.crcReturned;
          rcls = (RECTL *)malloc( numrcls * sizeof(RECTL) );
        } else if ( numrcls < rgnCtl.crcReturned )
        {
          numrcls = rgnCtl.crcReturned;
          rcls = (RECTL *)realloc( rcls, numrcls * sizeof(RECTL) );
        }

        rgnCtl.crcReturned = 0;
        rgnCtl.crc = numrcls;
        rgnCtl.ircStart = 0;
        
        err = GpiQueryRegionRects(hps, hrgn, NULL, &rgnCtl, rcls );

        GpiDestroyRegion(hps, hrgn);
      }

      WinReleasePS( hps );

      if ( !err )
      {
        DosReleaseMutexSem( offsetmapmux );
        break;
      }

      if ( !rgnCtl.crcReturned )
      {
        if ( DiveBlitRoutine == UseDiveBlitter )
        {
          DiveSetupBlitter( mglclient.diveinst, NULL );
        }
        DosReleaseMutexSem( offsetmapmux );
        break;
        // No region rectangles, so get outta here.
      }

      if ( DiveBlitRoutine != UseDiveBlitter )
      {
        int tmpwinposx, tmpwinposy, tmpwinsizex, tmpwinsizey;
        float multx, multy;

        // Custom blitter

        for ( i=0; i<validRegions; ++i )
        {
          if ( regions[i].offsetmapx )
            free( regions[i].offsetmapx );
          regions[i].offsetmapx = NULL;
          if ( regions[i].offsetmapy )
            free( regions[i].offsetmapy );
          regions[i].offsetmapy = NULL;
        }

        if ( numregions < numrcls )
        {
          if ( !numregions )
          {
            regions = (blitterRegion *)
             malloc( numrcls * sizeof( blitterRegion ) );
          } else {
            regions = (blitterRegion *)
             realloc( regions, numrcls * sizeof( blitterRegion ) );
          }
          numregions = numrcls;
        }

        WinQueryWindowPos(win, &swp);
        pointl.x=swp.x;
        pointl.y=swp.y;
        WinMapWindowPoints(WinQueryWindow( win, QW_PARENT ), HWND_DESKTOP,
         (POINTL*)&pointl, 1);

        tmpwinsizex = swp.cx;
        tmpwinsizey = swp.cy;
        tmpwinposx = pointl.x;
        tmpwinposy = mglclient.desktopheight - (pointl.y + swp.cy);

        for ( i=0; i<rgnCtl.crcReturned; ++i )
        {
          regions[i].winsizex = rcls[i].xRight - rcls[i].xLeft;
          regions[i].winsizey = rcls[i].yTop - rcls[i].yBottom;
          rcls[i].yTop = tmpwinsizey - rcls[i].yTop;
          rcls[i].yBottom = tmpwinsizey - rcls[i].yBottom;
          regions[i].winposx = tmpwinposx + rcls[i].xLeft;
          regions[i].winposy = tmpwinposy + rcls[i].yTop;

          regions[i].offsetmapx = (ULONG *)
           malloc( (regions[i].winsizex + 1) * 2 * sizeof( ULONG ) );
          regions[i].offsetmapy = (ULONG *)
           malloc( (regions[i].winsizey + 1) * sizeof( ULONG ) );

          mglclient.winsizex = swp.cx;
          mglclient.winsizey = swp.cy;

          multx = (float)(view_x2 - view_x1 + 1) / (float)mglclient.winsizex;
          multy = (float)(view_y2 - view_y1 + 1) / (float)mglclient.winsizey;

          regions[i].view_x1 = view_x1 + (rcls[i].xLeft * multx);
          regions[i].view_x2 = regions[i].view_x1 +
           (regions[i].winsizex * multx) - 1;
          regions[i].view_y1 = view_y1 + (rcls[i].yTop * multy);
          regions[i].view_y2 = regions[i].view_y1 +
           (regions[i].winsizey * multy) - 1;

          RemapOffsets( i );
        }

        validRegions = rgnCtl.crcReturned;

        DosReleaseMutexSem( offsetmapmux );
/*
        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "VRNENABLE done\n" );
          fflush( mglclient.dbfp );
        #endif
*/
        WinPostMsg( win, WM_PAINT, 0, 0 );

        break;
      }

      // Internal DIVE blitter

      err = 0; 
      WinQueryWindowPos(win, &swp);
      pointl.x=swp.x;
      pointl.y=swp.y;

      WinMapWindowPoints(WinQueryWindow( win, QW_PARENT ), HWND_DESKTOP,
       (POINTL*)&pointl, 1);

      mglclient.winposx = pointl.x;
      mglclient.winposy = mglclient.desktopheight - pointl.y - swp.cy;
      mglclient.winsizex = swp.cx;
      mglclient.winsizey = swp.cy;

      mglclient.BlSet.ulStructLen = sizeof( SETUP_BLITTER );
      mglclient.BlSet.fInvert = 0;
      mglclient.BlSet.fccSrcColorFormat = mglclient.depthfourcc;
      mglclient.BlSet.ulSrcWidth  = view_x2 - view_x1 + 1;
      mglclient.BlSet.ulSrcHeight = view_y2 - view_y1 + 1;
      mglclient.BlSet.ulSrcPosX = view_x1;
      mglclient.BlSet.ulSrcPosY = view_y1;
      mglclient.BlSet.ulDitherType = 1;
      mglclient.BlSet.fccDstColorFormat = FOURCC_SCRN;
      mglclient.BlSet.ulDstWidth=swp.cx;
      mglclient.BlSet.ulDstHeight=swp.cy;
      mglclient.BlSet.lDstPosX = 0;
      mglclient.BlSet.lDstPosY = 0;
      mglclient.BlSet.lScreenPosX=pointl.x;
      mglclient.BlSet.lScreenPosY=pointl.y;
      mglclient.BlSet.ulNumDstRects=rgnCtl.crcReturned;
      mglclient.BlSet.pVisDstRects=rcls;
      
      if ( rgnCtl.crcReturned > 39 )
      {
        mglclient.BlSet.ulNumDstRects = 39;
        // Hack to work around an internal DIVE limitation.
        // Update will not work properly, but at least it won't crash.
      }

      err = DiveSetupBlitter( mglclient.diveinst,
       &(mglclient.BlSet) );

      DosReleaseMutexSem( offsetmapmux );

      if ( err )
      {
/*
        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "VRNENABLE failed %lx.\n", err );
          fflush( mglclient.dbfp );
        #endif
*/
        return MRFROMSHORT(-1);
      }

      WinPostMsg( win, WM_PAINT, NULL, NULL );
    break;
    case WM_VRNDISABLE:
/*
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "VRNDISABLE\n" );
        fflush( mglclient.dbfp );
      #endif
*/
      if ( DiveBlitRoutine != UseDiveBlitter )
      {
        int i;

        // Custom blitter

        DosRequestMutexSem( offsetmapmux, SEM_INDEFINITE_WAIT );

        for ( i=0; i<validRegions; ++i )
        {
          if ( regions[i].offsetmapx )
            free( regions[i].offsetmapx );
          regions[i].offsetmapx = NULL;
          if ( regions[i].offsetmapy )
            free( regions[i].offsetmapy );
          regions[i].offsetmapy = NULL;
        }

        validRegions = 0;
        DosReleaseMutexSem( offsetmapmux );
/*
        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "VRNDISABLE done\n" );
          fflush( mglclient.dbfp );
        #endif
*/
        break;
      }
      DiveSetupBlitter( mglclient.diveinst, NULL );
    break;
    case WM_TOGGLEFS:
    {
      DosRequestMutexSem( reinit_mutex, SEM_INDEFINITE_WAIT );

      if ( mglclient.isFullScreen )
      {
        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "Switching to PM.  (sesID=0)\n" );
          fflush( mglclient.dbfp );
        #endif

        // DosSetPriority( PRTYS_THREAD, PRTYC_REGULAR, 0, 1 );
        // Make sure the foreground boost is undone when we switch back from
        // full screen mode.

        DosSelectSession( 0 );
      } else {
        int rc;

        if ( !MGLup )
        {
          #ifdef MGLSC_DEBUG_LOG
            fprintf( mglclient.dbfp, "Initializing MGL server.\n" );
            fflush( mglclient.dbfp );
          #endif
          
          mglclient.disallow_blit = 1;
          
          rc = MGLSC_MGL_init();
          if ( rc )
          {
            #ifdef MGLSC_DEBUG_LOG
              fprintf( mglclient.dbfp, "MGL server startup error #%d.\n", rc );
              fflush( mglclient.dbfp );
            #endif

            DosSelectSession( 0 );
            // Make sure we're back in the PM session.

            mglclient.isFullScreen = 0;
            // Client listener thread will get killed MGLServer has a problem.
            // Make sure we set the status back to PM mode because the listener
            // that would normally detect the mode switch is now deaf.

            DosReleaseMutexSem( reinit_mutex );
            break;
          }
          mglclient.disallow_blit = 0;
          MGLup = 1;
        } else {
          int rc;
          #ifdef MGLSC_DEBUG_LOG
            fprintf( mglclient.dbfp, "Switching to FS.  (sesID=%ld)\n", mglclient.MGL_session );
            fflush( mglclient.dbfp );
          #endif
          mglclient.disallow_blit = 1;
          rc = DosSelectSession( mglclient.MGL_session );
          if ( rc )
          {
            #ifdef MGLSC_DEBUG_LOG
              fprintf( mglclient.dbfp, "DosSelectSession returned %d!\n", rc );
            #endif
          }
          mglclient.disallow_blit = 0;
          // DosSetPriority( PRTYS_THREAD, PRTYC_FOREGROUNDSERVER, 0, 1 );
          // Make sure the process *generating* the graphics still gets its
          // foreground priority boost, like it would if it were the PM session
          // with the window focus.
        }
      }
      DosReleaseMutexSem( reinit_mutex );
    }
    break;
    case WM_COMMAND:
      if ( mglevents.PMMenuAction )
        mglevents.PMMenuAction( win, SHORT1FROMMP( mp1 ) );
    break;
    case WM_CLIPCAPTURE:
    {
      BITMAPINFOHEADER2 bmpi = {0};
      HBITMAP bmp;
      HPS hps;
      DEVOPENSTRUC dop = { 0, (PUCHAR)"DISPLAY", NULL, 0, 0, 0, 0, 0, 0 };
      SIZEL size = {0, 0};
      HDC hdc;
      unsigned char *bitmap24bit;
      int i, j, width, height;

      hdc = DevOpenDC( WinQueryAnchorBlock( mglclient.clientwin ),
       OD_MEMORY, (PUCHAR)"*", 5, (PDEVOPENDATA)&dop, NULLHANDLE );
      hps = GpiCreatePS( WinQueryAnchorBlock( mglclient.clientwin ),
       hdc, &size, PU_PELS | GPIA_ASSOC );

      width = view_x2 - view_x1 + 1;
      height = view_y2 - view_y1 + 1;

      bmpi.cbFix = sizeof(BITMAPINFOHEADER2);
      bmpi.cx = width;
      bmpi.cy = height;
      bmpi.cPlanes = 1;
      bmpi.cBitCount = 24;

      bitmap24bit = (unsigned char *)
       malloc( width * height * 3 );

      if ( mglclient.depth == 16 )
      {
        unsigned short *vidbuffer = mglclient.vidbuffer;
        for ( i=view_y1; i<=view_y2; ++i )
        {
          for ( j=view_x1; j<=view_x2; ++j )
          {
            bitmap24bit[ (((height-1)-(i-view_y1)) * width * 3)
             + ( (j-view_x1) * 3) + 0 ] =
              (vidbuffer[ (i*mglclient.width) + j ] & 0x1f) << 3;
            bitmap24bit[ (((height-1)-(i-view_y1)) * width * 3)
             + ( (j-view_x1) * 3) + 1 ] =
              ((vidbuffer[ (i*mglclient.width) + j ] >> 5) & 0x3f) << 2;
            bitmap24bit[ (((height-1)-(i-view_y1)) * width * 3)
             + ( (j-view_x1) * 3) + 2 ] =
              ((vidbuffer[ (i*mglclient.width) + j ] >> 11) & 0x1f) << 3;
          }
        }
      } else if ( mglclient.depth == 24 )
      {
        unsigned char *vidbuffer = mglclient.vidbuffer;
        for ( i=view_y1; i<=view_y2; ++i )
        {
          for ( j=view_x1; j<=view_x2; ++j )
          {
            bitmap24bit[ (((height-1)-(i-view_y1)) * width * 3)
             + ( (j-view_x1) * 3) + 0 ] = 
              vidbuffer[ ((i * mglclient.width) + j) * 3 ];
            bitmap24bit[ (((height-1)-(i-view_y1)) * width * 3)
             + ( (j-view_x1) * 3) + 1 ] = 
              vidbuffer[ (((i * mglclient.width) + j) * 3) + 1 ];
            bitmap24bit[ (((mglclient.height-1)-(i-view_y1)) * width * 3)
             + ( (j-view_x1) * 3) + 2 ] = 
              vidbuffer[ (((i * mglclient.width) + j) * 3) + 2 ];
          }
        }
      } else if ( mglclient.depth == 32 )
      {
        unsigned long *vidbuffer = mglclient.vidbuffer;
        for ( i=view_y1; i<=view_y2; ++i )
        {
          for ( j=view_x1; j<=view_x2; ++j )
          {
            bitmap24bit[ (((height-1)-(i-view_y1)) * width * 3)
             + ( (j-view_x1) * 3) + 0 ] =
              vidbuffer[ (i*mglclient.width) + j ] & 0xff;
            bitmap24bit[ (((height-1)-(i-view_y1)) * width * 3)
             + ( (j-view_x1) * 3) + 1 ] =
              (vidbuffer[ (i*mglclient.width) + j ] >> 8) & 0xff;
            bitmap24bit[ (((height-1)-(i-view_y1)) * width * 3)
             + ( (j-view_x1) * 3) + 2 ] =
              (vidbuffer[ (i*mglclient.width) + j ] >> 16) & 0xff;
          }
        }
      } else {
        MGL_SERVER_COLORS_SET_PACKET *cs = mglclient.shared_packet;
        unsigned char *vidbuffer = mglclient.vidbuffer;

        for ( i=view_y1; i<=view_y2; ++i )
        {
          for ( j=view_x1; j<=view_x2; ++j )
          {
            bitmap24bit[ (((height-1)-(i-view_y1)) * width * 3)
             + ( (j-view_x1) * 3) + 2 ] =
              cs->colors[ vidbuffer[ (i*mglclient.width) + j ] ].red;
            bitmap24bit[ (((height-1)-(i-view_y1)) * width * 3)
             + ( (j-view_x1) * 3) + 1 ] =
              cs->colors[ vidbuffer[ (i*mglclient.width) + j ] ].green;
            bitmap24bit[ (((height-1)-(i-view_y1)) * width * 3)
             + ( (j-view_x1) * 3) + 0 ] =
              cs->colors[ vidbuffer[ (i*mglclient.width) + j ] ].blue;
          }
        }
      }

      bmp = GpiCreateBitmap( hps, &bmpi, CBM_INIT, bitmap24bit,
       (BITMAPINFO2 *) &bmpi );
      free( bitmap24bit );

      if ( !bmp )
      {
        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "Error capturing bitmap!  GpiCreateBitmap error 0x%lx.\n",
           WinGetLastError( WinQueryAnchorBlock( mglclient.clientwin ) ) );
        #endif
        WinAlarm( HWND_DESKTOP, WA_ERROR );
        GpiDestroyPS(hps);
        DevCloseDC(hdc);
        return 0;
      }

      WinOpenClipbrd( WinQueryAnchorBlock( mglclient.clientwin ) );
      WinSetClipbrdOwner( WinQueryAnchorBlock( mglclient.clientwin ),
       mglclient.clientwin );
      WinEmptyClipbrd( WinQueryAnchorBlock( mglclient.clientwin ) );
      WinSetClipbrdData( WinQueryAnchorBlock( mglclient.clientwin ),
       bmp, CF_BITMAP, CFI_HANDLE );
      WinAlarm( HWND_DESKTOP, WA_WARNING );

      WinSetClipbrdOwner( WinQueryAnchorBlock( mglclient.clientwin ),
       NULLHANDLE );
      // Clipboard takes ownership of the handle and is responsible for
      // deleting the resources associated with it.

      WinCloseClipbrd( WinQueryAnchorBlock( mglclient.clientwin ) );
      GpiDestroyPS( hps );
      DevCloseDC( hdc );
    }
    break;
    case WM_MOUSEMOVE:
      if ( mouseGrab )
      {
        mouseMoved = 1;
        WinSetPointer( HWND_DESKTOP, 0 );
        return 0;
      }
    break;
    case WM_MOUSE_RECENTER:
      if ( mglclient.isFullScreen )
      {
        MGL_SERVER_MOUSE_POS_PACKET *mp = (MGL_SERVER_MOUSE_POS_PACKET *)
          mglclient.shared_packet;

        mp->newx = mglclient.fswidth / 2;
        mp->newy = mglclient.fsheight / 2;

        DosWriteQueue( mglclient.command_queue, MGLS_SET_MOUSE_POS,
          sizeof(MGL_SERVER_MOUSE_POS_PACKET), mp, 0 );
        // No need to sync this call
      } else {
        WinSetPointerPos( HWND_DESKTOP, PMCenterX, PMCenterY );
      }
    break;
    case WM_BUTTON1DOWN:
      if ( mglevents.ProcessInput )
        mglevents.ProcessInput( MGLSC_MOUSE_BUTTON_PRESS, 1, 0 );
    break;
    case WM_BUTTON1UP:
      if ( mglevents.ProcessInput )
        mglevents.ProcessInput( MGLSC_MOUSE_BUTTON_RELEASE, 1, 0 );
    break;
    case WM_BUTTON2DOWN:
      if ( mglevents.ProcessInput )
        mglevents.ProcessInput( MGLSC_MOUSE_BUTTON_PRESS, 2, 0 );
    break;
    case WM_BUTTON2UP:
      if ( mglevents.ProcessInput )
        mglevents.ProcessInput( MGLSC_MOUSE_BUTTON_RELEASE, 2, 0 );
    break;
    case WM_BUTTON3DOWN:
      if ( mglevents.ProcessInput )
        mglevents.ProcessInput( MGLSC_MOUSE_BUTTON_PRESS, 3, 0 );
    break;
    case WM_BUTTON3UP:
      if ( mglevents.ProcessInput )
        mglevents.ProcessInput( MGLSC_MOUSE_BUTTON_RELEASE, 3, 0 );
    break;
    case WM_FOCUSCHANGE:
      if ( mouseGrab && !SHORT1FROMMP(mp2) )
      {
        // Mouse is grabbed but we're losing the window focus.
        // Ungrab the mouse so the user can control their system.

        MGLSC_mouseGrab( 0 );
        
        mouseUngrabbedByFocus = 1;
      } else if ( mouseUngrabbedByFocus && SHORT1FROMMP(mp2) )
      {
        // Mouse grabbing was disabled by a focus change, but the focus
        // has changed back, so reenable grabbing.

        MGLSC_mouseGrab( 1 );
        mouseUngrabbedByFocus = 0;
      }
    break;
    case WM_SETWINSIZE:
    {
      RECTL framer, desktopr;
      SWP swp;
      ULONG swpopts = SWP_SIZE;
      USHORT scalex = SHORT1FROMMP(mp1);
      USHORT scaley = SHORT2FROMMP(mp1);

      framer.xLeft = 0;
      framer.xRight = ((view_x2 - view_x1) * scalex) / 1000;
      framer.yBottom = 0;
      framer.yTop = ((view_y2 - view_y1) * scaley) / 1000;
      WinCalcFrameRect( mglclient.framewin, &framer, FALSE );
      // Calculate needed frame rect from give client rect

      WinQueryWindowPos( mglclient.framewin, &swp );

      WinQueryWindowRect( HWND_DESKTOP, &desktopr );

      WinPostMsg( mglclient.clientwin, WM_VRNDISABLE, NULL, NULL );

      if ( SHORT1FROMMP( mp2 ) )
      {
        swpopts |= SWP_MOVE;

        WinSetWindowPos( mglclient.framewin, HWND_TOP,
          (((desktopr.xRight-desktopr.xLeft)/2)+desktopr.xLeft) -
          ((framer.xRight-framer.xLeft)/2),
          (((desktopr.yTop-desktopr.yBottom)/2)+desktopr.yBottom) -
          ((framer.yTop-framer.yBottom)/2),
          framer.xRight - framer.xLeft + 1,
          framer.yTop - framer.yBottom + 1,
          swpopts );
      } else {
        if ( swp.y + framer.yTop > desktopr.yTop )
        {
          framer.yBottom = swp.y - ((swp.y + (framer.yTop - framer.yBottom) + 1) - desktopr.yTop);
          framer.yTop = desktopr.yTop - 1;
          swpopts |= SWP_MOVE;
        }
        // Make sure titlebar doesn't go off the top of the screen
        if ( swp.y + framer.yTop < desktopr.yBottom )
        {
          framer.yBottom = swp.y + 50 + (desktopr.yBottom - (swp.y + (framer.yTop - framer.yBottom) + 1));
          framer.yTop = 49;
          swpopts |= SWP_MOVE;
        }
        // Make sure titlebar doesn't go off the bottom of the screen

        WinSetWindowPos( mglclient.framewin, HWND_TOP,
          framer.xLeft,
          framer.yBottom,
          framer.xRight - framer.xLeft + 1,
          framer.yTop - framer.yBottom + 1,
          swpopts );
      }
      WinPostMsg( mglclient.clientwin, WM_VRNENABLE, NULL, NULL );
    }
    break;
    case WM_SIZE:
      xscale = SHORT1FROMMP(mp2) * 1000 / (view_x2 - view_x1 + 1 );
      yscale = SHORT2FROMMP(mp2) * 1000 / (view_y2 - view_y1 + 1 );
      // Intentional fall-through
//    case WM_MOVE:
//      RemapOffsets();
    break;
  }

  if ( mglevents.AdditionalWindowProc )
  {
    return mglevents.AdditionalWindowProc( win, message, mp1, mp2 );
  } else {
    return WinDefWindowProc( win, message, mp1, mp2 );
  }
}


void PMThread( void )
{
  HAB ab;
  HMQ messq;
  QMSG qmsg;
  ULONG frameflgs= FCF_SIZEBORDER;
  RECTL framer, desktopr;

  if ( mglclient.windowdecor & MGLSC_WINDOW_DECOR_MENU )
   frameflgs |= FCF_MENU;
  if ( mglclient.windowdecor & MGLSC_WINDOW_DECOR_ICON )
   frameflgs |= FCF_ICON;
  if ( mglclient.windowdecor & MGLSC_WINDOW_DECOR_TASKLIST )
   frameflgs |= FCF_TASKLIST;
  if ( mglclient.windowdecor & MGLSC_WINDOW_DECOR_MINMAX )
   frameflgs |= FCF_MINMAX;
  if ( mglclient.windowdecor & MGLSC_WINDOW_DECOR_SYSMENU )
   frameflgs |= FCF_SYSMENU;
  if ( mglclient.windowdecor & MGLSC_WINDOW_DECOR_TITLEBAR )
   frameflgs |= FCF_TITLEBAR;

  ab = WinInitialize( 0 );

  messq = WinCreateMsgQueue( ab, 0 );

  WinRegisterClass( ab, (PUCHAR)mglclient.winclassname, DualModePM, 
    CS_SIZEREDRAW | CS_MOVENOTIFY, 0 );
        
  mglclient.framewin = WinCreateStdWindow( HWND_DESKTOP, WS_ANIMATE,
    &frameflgs, (PUCHAR)mglclient.winclassname,
    (PUCHAR)mglclient.wintitle, 0, 0, 1, &(mglclient.clientwin) );

  framer.xLeft = 0;
  framer.xRight = view_x2 - view_x1 + 1;
  framer.yBottom = 0;
  framer.yTop = view_y2 - view_y1 + 1;
  WinCalcFrameRect( mglclient.framewin, &framer, FALSE );
  // Calculate needed frame rect from give client rect

  WinQueryWindowRect( HWND_DESKTOP, &desktopr );

  WinPostMsg( mglclient.clientwin, WM_VRNDISABLE, NULL, NULL );

  WinSendMsg( mglclient.clientwin, WM_SETWINSIZE, MPFROM2SHORT( 1000, 1000 ),
   MPFROMSHORT( 1 ) );

  if ( mglevents.WindowInitializer )
    mglevents.WindowInitializer( mglclient.clientwin );
  // Let the application customize the window as desired

  WinShowWindow( mglclient.framewin, TRUE );
  WinSetFocus( HWND_DESKTOP, mglclient.clientwin );
  // WinPostMsg( mglclient.clientwin, WM_VRNENABLE, NULL, NULL );

  WinSetVisibleRegionNotify( mglclient.clientwin, TRUE );

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Entering message queue loop in PM thread.\n" );
    fflush( mglclient.dbfp );
  #endif

  while (WinGetMsg (ab, &qmsg, NULLHANDLE, 0, 0))
  {
    WinDispatchMsg (ab, &qmsg);
  }

  mglclient.autoBlitVidBufferOnRefresh = 0;

  if ( mglclient.diveinst )
  {
    if ( mglclient.divebufnum )
    {
      DiveFreeImageBuffer( mglclient.diveinst, mglclient.divebufnum );
      mglclient.divebufnum = 0;
    }
    DiveClose( mglclient.diveinst );
    mglclient.diveinst = 0;
  }

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Destroying window.\n" );
    fflush( mglclient.dbfp );
  #endif

  WinDestroyWindow( mglclient.framewin );
  mglclient.framewin = mglclient.clientwin = 0;

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Destroying message queue.\n" );
    fflush( mglclient.dbfp );
  #endif

  WinDestroyMsgQueue( messq );

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Terminating anchor block.\n" );
    fflush( mglclient.dbfp );
  #endif

  WinTerminate( ab );

  if ( mglclient.timer_thread )
  {
    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "Waiting for timing thread to end.\n" );
      fflush( mglclient.dbfp );
    #endif

    mglclient.framerate = -1;
    DosPostEventSem( mglclient.framerate_set_sem );
    DosReleaseMutexSem( mglclient.timing_mutex );
    // Just in case we own this mutex somehow

    if ( mglclient.timer_thread )
      DosWaitThread( &(mglclient.timer_thread), DCWW_WAIT );
  }

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Closing timing semaphores.\n" );
    fflush( mglclient.dbfp );
  #endif

  DosCloseEventSem( mglclient.framerate_set_sem );
  DosCloseEventSem( mglclient.timer_tick_sem );
  DosCloseMutexSem( mglclient.timing_mutex );
  DosCloseMutexSem( reinit_mutex );
  DosCloseMutexSem( offsetmapmux );

  while ( shuttingDown )
  {
    // If there are any active MGL sessions, wait for them to close before
    //  exitting this thread.
    DosSleep( 500 );
  }
  
  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Finished.  Closing debug file.\n" );
    fclose( mglclient.dbfp );
    mglclient.dbfp = NULL;
  #endif
  
  _endthread();
}

int setupBlitter( void )
{
  int i;

  for ( i=0; blitters[i].depth; ++i )
  {
    if ( *((ULONG *)blitters[i].srcFourCC) == mglclient.depthfourcc &&
         *((ULONG *)blitters[i].destFourCC) == mglclient.desktopFourCC )
    {
      #ifdef ALLOW_INTERNAL_DIVE_BLITTER
        if ( blitters[i].supportedByDIVE )
        {
          DiveBlitRoutine = UseDiveBlitter;

          #ifdef MGLSC_DEBUG_LOG
            fprintf( mglclient.dbfp, "Using DIVE internal blitter.\n" );
            fflush( mglclient.dbfp );
          #endif
          break;
        }
      #endif
      if ( mglclient.allowcustomblitter )
      {
        DiveBlitRoutine = blitters[i].directBlitter;

        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "Input format is not supported by DIVE.  Using a custom blitter (%c%c%c%c -> %c%c%c%c).\n",
           blitters[i].srcFourCC[0], blitters[i].srcFourCC[1],
           blitters[i].srcFourCC[2], blitters[i].srcFourCC[3],
           blitters[i].destFourCC[0], blitters[i].destFourCC[1],
           blitters[i].destFourCC[2], blitters[i].destFourCC[3] );
          fflush( mglclient.dbfp );
        #endif
      }

      break;
    }
  }

  if ( DiveBlitRoutine == NULL )
  {
    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "Could not find an appropriate blitter to blit from %d bpp to %d bpp!\n",
        mglclient.depth, mglclient.desktopdepth );
      fflush( mglclient.dbfp );
    #endif
    return -1;
  }

  return 0;
}

int MGLSC_init( int width, int height, int depth, int formatFlags,
 int view_leftx, int view_topy, int view_rightx, int view_bottomy,
 struct MGLSC_event_handlers *eventhandler, char *winclassname,
 char *wintitle, struct vidmode_descrip *available_modes,
 BOOL windowDecorations, BOOL allowCustomModes,
 BOOL useJoystickIfAvailable )
{
  memset( &mglclient, 0, sizeof( mglclient ) );

  DosCreateEventSem( NULL, &mglclient.framerate_set_sem, 0, FALSE );
  DosCreateEventSem( NULL, &mglclient.timer_tick_sem, 0, FALSE );
  DosCreateMutexSem( NULL, &mglclient.timing_mutex, 0, FALSE );
  DosCreateMutexSem( NULL, &offsetmapmux, 0, FALSE );
  
  view_x1 = view_leftx;
  view_x2 = view_rightx;
  view_y1 = view_topy;
  view_y2 = view_bottomy;

  if ( view_x2 <= view_x1 ) view_x2 = view_x1 + 1;
  if ( view_y2 <= view_y1 ) view_y2 = view_y1 + 1;
  if ( view_x1 < 0 ) view_x1 = 0;
  if ( view_x2 >= width ) view_x2 = width - 1;
  if ( view_y1 < 0 ) view_y1 = 0;
  if ( view_y2 >= height ) view_y2 = height - 1;
  if ( view_x2 <= view_x1 ) view_x2 = view_x1 + 1;

  #ifdef MGLSC_DEBUG_LOG
    mglclient.dbfp = fopen( "mglsc_debug.log", "wa" );
  #endif

  mglclient.allowedmodes = available_modes;
  mglclient.custommodes = allowCustomModes;
  mglclient.windowdecor = windowDecorations;

  memcpy( &mglevents, eventhandler, sizeof( struct MGLSC_event_handlers ) );

  mglclient.winclassname = winclassname;
  mglclient.wintitle = wintitle;
  mglclient.depthfourcc = 0;
  mglclient.formatFlags = formatFlags;

  switch ( depth )
  {
    case 8:
      if ( formatFlags & FORMAT_LOOKUPTABLE &&
           !(formatFlags & FORMAT_BGR) )
        mglclient.depthfourcc = FOURCC_LUT8;
    break;
    case 15:
      if ( formatFlags & FORMAT_LOOKUPTABLE )
        mglclient.depthfourcc = FOURCC_LT12;
      else if ( !(formatFlags & FORMAT_BGR) )
        mglclient.depthfourcc = FOURCC_R555;
    break;
    case 16:
      if ( formatFlags & FORMAT_LOOKUPTABLE )
        mglclient.depthfourcc = FOURCC_LT12;
      else if ( !(formatFlags & FORMAT_BGR) )
        mglclient.depthfourcc = FOURCC_R565;
    break;
    case 24:
      if ( !(formatFlags & FORMAT_LOOKUPTABLE) )
      {
        if ( !(formatFlags & FORMAT_BGR) )
          mglclient.depthfourcc = FOURCC_RGB3;
        else
          mglclient.depthfourcc = FOURCC_BGR3;
      }
    break;
    case 32:
      if ( !(formatFlags & FORMAT_LOOKUPTABLE) )
      {
        if ( !(formatFlags & FORMAT_BGR) )
          mglclient.depthfourcc = FOURCC_RGB4;
        else
          mglclient.depthfourcc = FOURCC_BGR4;
      }
    break;
    default:
      return MGLSC_UNSUPPORTED_COLOR_DEPTH;
  }

  ReportDIVECaps( DIVE_BUFFER_SCREEN );

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Request to initialize video mode to %d x %d %dbpp.\n", 
      width, height, depth );
    fflush( mglclient.dbfp );
  #endif

  mglclient.width = width;
  mglclient.height = height;
  mglclient.depth = depth;

  if ( setupBlitter() ) return MGLSC_UNABLE_TO_FIND_BLITTER;

  mglclient.isFullScreen = 0;

  DosAllocSharedMem( &(mglclient.vidbuffer), NULL, 
    width * height * (depth/8), OBJ_GETTABLE | PAG_READ | PAG_WRITE | PAG_COMMIT );

  DosAllocSharedMem( &(mglclient.shared_packet), NULL, 
    MGLS_BIGGEST_PACKET_SIZE, OBJ_GETTABLE | PAG_READ | PAG_WRITE | PAG_COMMIT );

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Starting PM Thread.\n" );
    fflush( mglclient.dbfp );
  #endif

  mglclient.disallow_blit = 1;

  mglclient.pm_thread =
    _beginthread( (void(*)(void*))PMThread, NULL, 32768, NULL );

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "PM Thread ID = %ld.\n", mglclient.pm_thread );
    fflush( mglclient.dbfp );
  #endif

  if ( useJoystickIfAvailable )
  {
    if ( !ejoy_open() )
    {
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "Joystick driver opened.\n" );
        fflush( mglclient.dbfp );
      #endif

      if ( !ejoy_check_version() )
      {
        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "Unsupported version of joystick driver.\n" );
          fflush( mglclient.dbfp );
        #endif

        ejoy_close();
        hEJoy = 0;
      }
    }
  }

  while ( !mglclient.disallow_blit ) DosSleep( 500 );
  // Lazy man's semaphore.

  // DIVE is fully initialized now.  It's safe to return.
  
  return MGLSC_NO_ERROR;
}

int MGLSC_reinit( int width, int height, int depth, int formatFlags,
 BOOL allowCustomModes, BOOL useJoystickIfAvailable )
{
  ULONG err;

  mglclient.custommodes = allowCustomModes;

  if ( !useJoystickIfAvailable && hEJoy )
  {
    ejoy_close();
    hEJoy = 0;
  } else if ( useJoystickIfAvailable && !hEJoy )
  {
    if ( !ejoy_open() )
    {
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "Joystick driver opened.\n" );
        fflush( mglclient.dbfp );
      #endif

      if ( !ejoy_check_version() )
      {
        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "Unsupported version of joystick driver.\n" );
          fflush( mglclient.dbfp );
        #endif

        ejoy_close();
        hEJoy = 0;
      }
    }
  }

  if ( width != mglclient.width || height != mglclient.height ||
       depth != mglclient.depth || formatFlags != mglclient.formatFlags )
  {
    // Screen mode change

    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "Request to change video mode to %d x %d %dbpp.\n", 
        width, height, depth );
      fflush( mglclient.dbfp );
    #endif

    if ( DiveBlitRoutine == UseDiveBlitter )
    {
      DiveSetupBlitter( mglclient.diveinst, NULL );
      // Stop DIVE blitting while we reallocate buffer sizes

      mglclient.disallow_blit = 1;
      // Stop MGL & DIVE blits

      DiveFreeImageBuffer( mglclient.diveinst, mglclient.divebufnum );
      mglclient.divebufnum = 0;
    }

    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "DIVE %s been disabled.\n",
       MGLup ? "and MGL blitters have" : "blitter has" );
      fflush( mglclient.dbfp );
    #endif

    DosFreeMem( mglclient.vidbuffer );

    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "Old video buffer has been freed.\n" );
      fflush( mglclient.dbfp );
    #endif

    switch ( depth )
    {
      case 8:
        if ( formatFlags & FORMAT_LOOKUPTABLE &&
             !(formatFlags & FORMAT_BGR) )
          mglclient.depthfourcc = FOURCC_LUT8;
      break;
      case 15:
        if ( formatFlags & FORMAT_LOOKUPTABLE )
          mglclient.depthfourcc = FOURCC_LT12;
        else if ( !(formatFlags & FORMAT_BGR) )
          mglclient.depthfourcc = FOURCC_R555;
      break;
      case 16:
        if ( formatFlags & FORMAT_LOOKUPTABLE )
          mglclient.depthfourcc = FOURCC_LT12;
        else if ( !(formatFlags & FORMAT_BGR) )
          mglclient.depthfourcc = FOURCC_R565;
      break;
      case 24:
        if ( !(formatFlags & FORMAT_LOOKUPTABLE) )
        {
          if ( !(formatFlags & FORMAT_BGR) )
            mglclient.depthfourcc = FOURCC_RGB3;
          else
            mglclient.depthfourcc = FOURCC_BGR3;
        }
      break;
      case 32:
        if ( !(formatFlags & FORMAT_LOOKUPTABLE) )
        {
          if ( !(formatFlags & FORMAT_BGR) )
            mglclient.depthfourcc = FOURCC_RGB4;
          else
            mglclient.depthfourcc = FOURCC_BGR4;
        }
      break;
      default:
        return MGLSC_UNSUPPORTED_COLOR_DEPTH;
    }

    DosAllocSharedMem( &(mglclient.vidbuffer), NULL,
     width * height * (depth/8), OBJ_GETTABLE | PAG_READ | PAG_WRITE |
     PAG_COMMIT );

    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "New video buffer allocated.\n" );
      fflush( mglclient.dbfp );
    #endif

    mglclient.width = width;
    mglclient.height = height;
    mglclient.depth = depth;
    mglclient.formatFlags = formatFlags;

    if ( setupBlitter() ) return MGLSC_UNABLE_TO_FIND_BLITTER;

    if ( DiveBlitRoutine == UseDiveBlitter )
    {
      err = DiveAllocImageBuffer( mglclient.diveinst, 
        &(mglclient.divebufnum), mglclient.depthfourcc,
        width, height, width * (depth / 8), 
        (PBYTE *) mglclient.vidbuffer );

      if ( err )
      {
        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "ERROR!: DiveAllocImageBuffer returned %lx.\n", err );
          fflush( mglclient.dbfp );
        #endif

        DiveClose( mglclient.diveinst );
        mglclient.diveinst = 0;
        return MGLSC_UNABLE_TO_ALLOCATE_IMAGE_BUFFER;
      }
    }

    if ( view_x2 >= mglclient.width  ) view_x2 = mglclient.width  - 1;
    if ( view_y2 >= mglclient.height ) view_y2 = mglclient.height - 1;
    // Shrink the viewport if necessary to fit new video buffer size

    WinPostMsg( mglclient.clientwin, WM_SETWINSIZE, MPFROM2SHORT(xscale,yscale), 0 );
    // Resize the window if needed

    // This should take care of the PM/DIVE aspect, now deal with making
    // the changes to the MGL session if it is up.

    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "DIVE settings were changed.\n" );
      fflush( mglclient.dbfp );
    #endif

    if ( MGLup )
    {
      if ( !mglclient.isFullScreen )
      {
        mglclient.vidmode_reinit = 1;
        // Have to wait until the MGL session is reactivated to change full
        // screen video mode.
        mglclient.disallow_blit = 0;
        // Allow the window to blit in the meantime.
      } else {
        MGLSC_MGL_reinit();
        mglclient.disallow_blit = 0;
        // re-allow blitting
      }
    } else {
      mglclient.disallow_blit = 0;
      // Since MGL isn't up yet, we should be all set by this point to
      // begin blitting again.
    }
  }
  
  return MGLSC_NO_ERROR;
}

static void MGLSC_MGL_reinit( void )
{
  int i;
  MGL_SERVER_INIT_VIDMODE_PACKET *iv;
  MGL_SERVER_INIT_BUFFER_PACKET *ib;

  DosRequestMutexSem( reinit_mutex, SEM_INDEFINITE_WAIT );

  iv = (MGL_SERVER_INIT_VIDMODE_PACKET *) mglclient.shared_packet;
  iv->flags = 0;

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Current FS mode %d x %d %dbpp.\n", 
      mglclient.fswidth, mglclient.fsheight, mglclient.fsdepth );
    fflush( mglclient.dbfp );
  #endif

  for ( i=0; mglclient.allowedmodes[i].width; ++i )
  {
    if ( mglclient.allowedmodes[i].width < view_x2 - view_x1 + 1 ) continue;
    if ( mglclient.allowedmodes[i].height < view_y2 - view_y1 + 1 ) continue;
    if ( mglclient.allowedmodes[i].depth < mglclient.depth ) continue;

    if ( mglclient.allowedmodes[i].width  == mglclient.fswidth &&
         mglclient.allowedmodes[i].height == mglclient.fsheight &&
         mglclient.allowedmodes[i].depth  == mglclient.fsdepth )
    {
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "Found FS mode %d x %d %dbpp.\n", 
          mglclient.allowedmodes[i].width, mglclient.allowedmodes[i].height,
          mglclient.allowedmodes[i].depth );
        fprintf( mglclient.dbfp, "Full screen video mode change not needed.\n" ); 
        fflush( mglclient.dbfp );
      #endif

      iv->flags = MGLS_VMOFLAG_SUCCESS;
      break;
    } else {
      // Found a suitable video mode that is not the current video mode
      // If we found it first in the available modes list, then we can
      // assume that it is a tighter fit to what the app wants, so we'll
      // choose it in preference to the current video mode.

      break;
    }
  }

  if ( iv->flags )
  {
    // We're already in the desired video mode.  Just make sure the
    // MGL side sees the possibly new backbuffer address and clean up
    // access to the old one.

    MGLS_synchronous_command( MGLS_FREE_BUFFER,
      sizeof(MGL_SERVER_FREE_BUFFER_PACKET), mglclient.shared_packet );
    // First free up the old one.  The shared packet still points to it.

    ib = (MGL_SERVER_INIT_BUFFER_PACKET *) mglclient.shared_packet;
    ib->buffer = mglclient.vidbuffer;
    ib->width = mglclient.width;
    ib->height = mglclient.height;
    ib->depth = mglclient.depth;

    MGLS_synchronous_command( MGLS_INIT_BUFFER, 
      sizeof(MGL_SERVER_INIT_BUFFER_PACKET), ib );
    // Now give it access to the new buffer and we're all set.

    DosReleaseMutexSem( reinit_mutex );

    mglclient.vidmode_reinit = 0;
    mglclient.disallow_blit = 0;
    // Video mode is already set as needed so we're done.

    return;
  }

  // Above we do a quick search to see if the mode we want is the one we've
  // already got.  If not, we wind up here and have to change the mode to
  // something different, so start by going back to text mode...

  MGLS_synchronous_command( MGLS_FREE_BUFFER,
    sizeof(MGL_SERVER_FREE_BUFFER_PACKET), mglclient.shared_packet );
  MGLS_synchronous_command( MGLS_SHUTDOWN_VIDMODE,
    sizeof(MGL_SERVER_SHUTDOWN_VIDMODE_PACKET), mglclient.shared_packet );
  // Gets us back to text mode and frees up old resources

  // Now enter that loop again, only this time, try out suitable new modes
  // as we find them.

  iv->flags = 0;

  for ( i=0; mglclient.allowedmodes[i].width; ++i )
  {
    if ( mglclient.allowedmodes[i].width < view_x2 - view_x1 + 1 ) continue;
    if ( mglclient.allowedmodes[i].height < view_y2 - view_y1 + 1 ) continue;
    if ( mglclient.allowedmodes[i].depth < mglclient.depth ) continue;

    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "Attempting FS mode %d x %d %dbpp.\n", 
        mglclient.allowedmodes[i].width, mglclient.allowedmodes[i].height,
        mglclient.allowedmodes[i].depth );
      fflush( mglclient.dbfp );
    #endif

    iv->width = mglclient.allowedmodes[i].width;
    iv->height = mglclient.allowedmodes[i].height;
    iv->depth = mglclient.allowedmodes[i].depth;
    iv->flags = (mglclient.custommodes ? MGLS_VMIFLAG_USE_CUSTOM : 0);

    MGLS_synchronous_command( MGLS_INIT_VIDMODE, 
      sizeof(MGL_SERVER_INIT_VIDMODE_PACKET), iv );

    if ( iv->flags & MGLS_VMOFLAG_SUCCESS ) break;
  }

  if ( !(iv->flags & MGLS_VMOFLAG_SUCCESS) )
  {
    MGLS_synchronous_command( MGLS_SHUTDOWN,
      sizeof(MGL_SERVER_SHUTDOWN_PACKET), mglclient.shared_packet );

    DosSelectSession( 0 );

    DosKillThread( mglclient.listener_thread );
    DosCloseQueue( mglclient.command_queue );
    DosCloseQueue( mglclient.listener_queue );

    DosCloseEventSem( mglclient.client_wakeup );
    DosReleaseMutexSem( reinit_mutex );
    return;
    // Could not use any acceptable modes
  }

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Success using FS mode %d x %d %dbpp.\n", 
      mglclient.allowedmodes[i].width, mglclient.allowedmodes[i].height,
      mglclient.allowedmodes[i].depth );
    fflush( mglclient.dbfp );
  #endif

  mglclient.fswidth  = mglclient.allowedmodes[i].width;
  mglclient.fsheight = mglclient.allowedmodes[i].height;
  mglclient.fsdepth  = mglclient.allowedmodes[i].depth;

  ib = (MGL_SERVER_INIT_BUFFER_PACKET *) mglclient.shared_packet;
  ib->buffer = mglclient.vidbuffer;
  ib->width = mglclient.width;
  ib->height = mglclient.height;
  ib->depth = mglclient.depth;

  if ( mglclient.depth == 8 )
  {
    mglclient.palette_dirty = 1;
    DosWriteQueue( mglclient.command_queue, MGLS_SET_COLORS,
     sizeof(MGL_SERVER_COLORS_SET_PACKET), mglclient.shared_packet, 0 );
  }

  MGLS_synchronous_command( MGLS_INIT_BUFFER, 
    sizeof(MGL_SERVER_INIT_BUFFER_PACKET), ib );

  mglclient.vidmode_reinit = 0;
  mglclient.disallow_blit = 0;
  // Video mode is all set so reset these flags

  DosReleaseMutexSem( reinit_mutex );
}

BOOL MGLSC_isJoystickPresent( void )
{
  return enhanced_joystick_interface;
}

BOOL MGLSC_getNumJoysticks( void )
{
  ejoy_ext_get_caps( 0 );
  return ejoy_caps.nro_of_joysticks;
}

BOOL MGLSC_getNumJoyButtons( LONG whichStick )
{
  return num_joysticks;
}

void MGLSC_mouseGrab( BOOL yesNo )
{
  static ULONG oldX1, oldY1, oldX2, oldY2;

  mouseUngrabbedByFocus = 0;

  if ( (mouseGrab && !yesNo) || (!mouseGrab && yesNo) )
  {
    mouseGrab = yesNo;

    if ( !mouseGrab )
    {
      WinSetCapture( HWND_DESKTOP, 0 );
      setMouseConstrainedRegion( oldX1, oldY1, oldX2, oldY2 );
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "Mouse constrained region restored: %ld,%ld - %ld,%ld\n",
         oldX1, oldY1, oldX2, oldY2 );
        fflush( mglclient.dbfp );
      #endif
    } else {
      int x1, y1, x2, y2;
      WinSetCapture( HWND_DESKTOP, mglclient.clientwin );
      WinSetPointer( HWND_DESKTOP, 0 );
      getMouseConstrainedRegion( &x1, &y1, &x2, &y2 );
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "Old mouse constrained region: %d,%d - %d,%d\n",
         x1, y1, x2, y2 );
      #endif
      oldX1 = x1; oldY1 = y1; oldX2 = x2; oldY2 = y2;
      setMouseConstrainedRegion( 0, 0, mglclient.width - 1,
      mglclient.height - 1 );
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "New mouse constrained region: 0,0 - %d,%d\n",
         mglclient.width - 1, mglclient.height - 1 );
        fflush( mglclient.dbfp );
      #endif
    }
  }
}

void MGLSC_setMouseModeRelative( BOOL isRelative )
{
  mouseRel = isRelative;
  if ( !isRelative )
  {
    mglevents.ProcessInput( MGLSC_MOUSE_MOVE, mglclient.width / 2,
     mglclient.height / 2 );
    // Make sure client knows that the mouse absolute position has been
    // re-centered the next time it asks to flush the event queue.
  }
}

void MGLSC_setViewPort( int view_leftx, int view_topy, int view_rightx,
 int view_bottomy, BOOL pickBestVidMode )
{
  char sizechanged = 0;

  if ( view_x2 - view_x1 != view_rightx - view_leftx ||
       view_y2 - view_y1 != view_bottomy - view_topy )
  {
    sizechanged = 1;
  }

  view_x1 = view_leftx;
  view_x2 = view_rightx;
  view_y1 = view_topy;
  view_y2 = view_bottomy;

  if ( view_x2 <= view_x1 ) view_x2 = view_x1 + 1;
  if ( view_y2 <= view_y1 ) view_y2 = view_y1 + 1;
  if ( view_x1 < 0 ) view_x1 = 0;
  if ( view_x2 >= mglclient.width ) view_x2 = mglclient.width - 1;
  if ( view_y1 < 0 ) view_y1 = 0;
  if ( view_y2 >= mglclient.height ) view_y2 = mglclient.height - 1;
  if ( view_x2 <= view_x1 ) view_x2 = view_x1 + 1;

  if ( sizechanged )
  {
    WinPostMsg( mglclient.clientwin, WM_SETWINSIZE, MPFROM2SHORT( xscale, yscale ), 0 );
    // Set the window size.  Preserve the scaling values of the window in case the
    // user resized it to an arbitrary size.  Try to maintain the same proportions.

    if ( MGLup )
    {
      if ( view_x2 - view_x1 >= mglclient.fswidth ||
           view_y2 - view_y1 >= mglclient.fsheight ||
           pickBestVidMode )
      {
        // Need to pick a different screen mode to accommodate the new viewport
        if ( !mglclient.isFullScreen )
        {
          mglclient.vidmode_reinit = 1;
          // Have to wait until the MGL session is reactivated to change full
          // screen video mode.
        } else {
          MGLSC_MGL_reinit();
        }
      }
    }

  } else {
    WinPostMsg( mglclient.clientwin, WM_VRNENABLE, 0, 0 );
    // Make the DIVE blitter acknowledge the new settings.
  }
}

void MGLSC_setMousePosition( int x, int y )
{
  if ( mglclient.isFullScreen )
  {
    MGL_SERVER_MOUSE_POS_PACKET *mp = (MGL_SERVER_MOUSE_POS_PACKET *)
      mglclient.shared_packet;

    mp->newx = x;
    mp->newy = y;

    DosWriteQueue( mglclient.command_queue, MGLS_SET_MOUSE_POS,
      sizeof(MGL_SERVER_MOUSE_POS_PACKET), mp, 0 );
    // No need to sync this call
  } else {
    WinSetPointerPos( HWND_DESKTOP, x, y );
  }
}

void MGLSC_setColor( int coloridx, int red, int green, int blue )
{
  MGL_SERVER_COLORS_SET_PACKET *cs = (MGL_SERVER_COLORS_SET_PACKET *)
   mglclient.shared_packet;

  if ( coloridx > 255 ) return;

  cs->colors[coloridx].red   = red;
  cs->colors[coloridx].green = green;
  cs->colors[coloridx].blue  = blue;

  mglclient.palette_dirty = 1;
}

void MGLSC_getColor( int coloridx, int *red, int *green, int *blue )
{
  MGL_SERVER_COLORS_SET_PACKET *cs = (MGL_SERVER_COLORS_SET_PACKET *)
   mglclient.shared_packet;

  if ( coloridx > 255 ) return;

  *red   = cs->colors[coloridx].red;
  *green = cs->colors[coloridx].green;
  *blue  = cs->colors[coloridx].blue;
}

void TimerThread( void *junk )
{
  ULONG ratechanged, waittime, waitbase, tmp, size;
  double waitadder, waitleftovers;

  DosSetPriority( PRTYS_THREAD, PRTYC_TIMECRITICAL, 0x1f, 0 );

  DosWaitEventSem( mglclient.framerate_set_sem, SEM_INDEFINITE_WAIT );

  while ( 1 )
  {
    if ( mglclient.framerate == -1 ) break;
    if ( mglclient.framerate == 0 || !mglclient.timer0 )
    {
      DosResetEventSem( mglclient.framerate_set_sem, &ratechanged );
      DosWaitEventSem( mglclient.framerate_set_sem, SEM_INDEFINITE_WAIT );
      continue;
    }

    DosResetEventSem( mglclient.framerate_set_sem, &ratechanged );
    ratechanged = 0;

    waitbase = 1000 / mglclient.framerate;
    waittime = waitbase;
    waitleftovers = 0;
    waitadder = ((double)((double)(1000) / (double)(mglclient.framerate))) -
     (double)waittime;

    while ( !ratechanged )
    {
      DosRequestMutexSem( mglclient.timing_mutex, SEM_INDEFINITE_WAIT );

      if ( mglclient.timer0 )
      {
        size = sizeof(ULONG);
        if ( (DosDevIOCtl( mglclient.timer0, HRT_IOCTL_CATEGORY,
         HRT_BLOCKUNTIL, &waittime, size, &size, NULL, 0, NULL)) != 0)
        {
          DosClose( mglclient.timer0 );
          mglclient.timer0 = 0;
          #ifdef MGLSC_DEBUG_LOG
            fprintf( mglclient.dbfp, "Error using Timer0 device.\n" );
            fflush( mglclient.dbfp );
          #endif
        }
        // mglclient.laggedby++;
      } else {
        DosReleaseMutexSem( mglclient.timing_mutex );
        break;
      }

      DosReleaseMutexSem( mglclient.timing_mutex );
      DosPostEventSem( mglclient.timer_tick_sem );

      waitleftovers += waitadder;
      waittime = waitbase;

      tmp = (ULONG)waitleftovers;
      if ( tmp )
      {
        // in other words, if it's 1 or higher
        waitleftovers -= tmp;
        waittime += tmp;
        // every millisecond counts!  ;-)
      }

      DosRequestMutexSem( mglclient.timing_mutex, SEM_INDEFINITE_WAIT );
      DosResetEventSem( mglclient.framerate_set_sem, &ratechanged );
      DosReleaseMutexSem( mglclient.timing_mutex );
    }
  }

  if ( mglclient.timer0 )
  {
    DosClose( mglclient.timer0 );
    mglclient.timer0 = 0;
  }

  mglclient.timer_thread = 0;

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Timing thread exitting.\n" );
    fflush( mglclient.dbfp );
  #endif

  _endthread();
}

void MGLSC_setTimingMode( BOOL useTimer0 )
{
  DosRequestMutexSem( mglclient.timing_mutex, SEM_INDEFINITE_WAIT );

  if ( useTimer0 && !mglclient.timer0 )
  {
    ULONG action;
    ULONG ulOpenFlag = OPEN_ACTION_OPEN_IF_EXISTS;
    ULONG ulOpenMode = OPEN_FLAGS_FAIL_ON_ERROR | OPEN_SHARE_DENYNONE |
                       OPEN_ACCESS_READWRITE;

    if ( DosOpen( (PSZ)"TIMER0$", &(mglclient.timer0), &action, 0, 0,
          ulOpenFlag, ulOpenMode, NULL ) )
    {
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "Error enabling Timer0 device.\n" );
        fflush( mglclient.dbfp );
      #endif
      mglclient.timer0 = 0;
    }
  } else if ( !useTimer0 && mglclient.timer0 )
  {
    DosClose( mglclient.timer0 );
    mglclient.timer0 = 0;
  }

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Using %s for frame rate timing.\n",
     mglclient.timer0 ? "Timer0" : "DosSleep" );
    fflush( mglclient.dbfp );
  #endif

  DosReleaseMutexSem( mglclient.timing_mutex );

  if ( mglclient.timer_thread )
  {
    mglclient.curskip = 0;
    mglclient.laggedby = 0;

    if ( !mglclient.timer0 )
    {
      DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &pollstarttime, 4 );
    } else {
      DosPostEventSem( mglclient.framerate_set_sem );
      // Tells the timer thread to wake up and start using Timer0 again.
    }
  } else {
    mglclient.timer_thread = _beginthread( (void(*)(void*))TimerThread,
     NULL, 8192, NULL );

    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "Launching timing thread (#%ld).\n",
       mglclient.timer_thread );
      fflush( mglclient.dbfp );
    #endif
  }
}

void MGLSC_setFrameRate( int frames_per_second )
{
  mglclient.framerate = frames_per_second;
  mglclient.curskip = 0;
  mglclient.laggedby = 0;

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Frame rate set to %d fps.\n",
     frames_per_second );
    fflush( mglclient.dbfp );
  #endif

  DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &pollstarttime, 4 );
  polltime = 1000 / frames_per_second;
  pollbase = polltime;
  pollleftovers = 0;
  polladder = ((double)((double)(1000) / (double)(mglclient.framerate))) -
   (double)polltime;

  DosPostEventSem( mglclient.framerate_set_sem );
}

void MGLSC_setMaxFrameSkip( int max_frames_to_skip )
{
  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Max frame skip set to %d.\n",
     max_frames_to_skip );
    fflush( mglclient.dbfp );
  #endif

  mglclient.maxskip = max_frames_to_skip;
  mglclient.curskip = 0;
  mglclient.laggedby = 0;
}

void MGLSC_blockUntilFrameReady( void )
{
  ULONG currentmscount;

  if ( mglclient.timer0 )
  {
    DosWaitEventSem( mglclient.timer_tick_sem, SEM_INDEFINITE_WAIT );
  } else {
    DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &currentmscount, 4 );
    while ( currentmscount < (pollstarttime + polltime) )
    {
      DosSleep( 1 );
      DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &currentmscount, 4 );
    }
  }
}

BOOL MGLSC_shouldSkipFrame( void )
{
  static ULONG currentmscount;

  if ( mglclient.timer0 )
  {
    if ( mglclient.laggedby && (mglclient.curskip > mglclient.maxskip) )
    {
      // Skipping too many frames in a row.  Draw this one.
      mglclient.laggedby = 0;
    }
  } else {
    DosQuerySysInfo( QSV_MS_COUNT, QSV_MS_COUNT, &currentmscount, 4 );
    if ( currentmscount > (pollstarttime + polltime) )
      mglclient.laggedby = 1;
    else
      mglclient.laggedby = 0;
  }

  return mglclient.laggedby != 0;
}

void MGLSC_advanceFrame( BOOL skipped )
{
  ULONG postcount, tmp;
  
  if ( mglclient.activeKeyDef && mglclient.activeKeyDef <= 105 &&
        mglevents.ProcessInput )
  {
    tmp = 1;
    
    while ( tmp )
    {
      switch ( KEYDEF_INSTR( keyDefs[ mglclient.activeKeyDef ].
                instructions[ mglclient.keyDefInstruction ] ) )
      {
        case KEYDEF_PRESS:
          mglevents.ProcessInput( MGLSC_KEYBOARD_MAKE,
           KEYDEF_PARAM( keyDefs[ mglclient.activeKeyDef ].
            instructions[ mglclient.keyDefInstruction ] ), 0 );
          #ifdef MGLSC_DEBUG_LOG
          #ifdef MGLSC_DEBUG_PROGKEY
            fprintf( mglclient.dbfp, "KEYDEF %d - [%d]: PRESS %d\n",
             mglclient.activeKeyDef, mglclient.keyDefInstruction,
             KEYDEF_PARAM( keyDefs[ mglclient.activeKeyDef ].
              instructions[ mglclient.keyDefInstruction ] ) );
            fflush( mglclient.dbfp );
          #endif
          #endif
          mglclient.keyDefInstruction++;
        break;
        case KEYDEF_RELEASE:
          mglevents.ProcessInput( MGLSC_KEYBOARD_BREAK,
           KEYDEF_PARAM( keyDefs[ mglclient.activeKeyDef ].
            instructions[ mglclient.keyDefInstruction ] ), 0 );
          #ifdef MGLSC_DEBUG_LOG
          #ifdef MGLSC_DEBUG_PROGKEY
            fprintf( mglclient.dbfp, "KEYDEF %d - [%d]: RELEASE %d\n",
             mglclient.activeKeyDef, mglclient.keyDefInstruction,
             KEYDEF_PARAM( keyDefs[ mglclient.activeKeyDef ].
              instructions[ mglclient.keyDefInstruction ] ) );
            fflush( mglclient.dbfp );
          #endif
          #endif
          mglclient.keyDefInstruction++;
        break;
        case KEYDEF_REPEAT:
          // Avoid potential tight infinite loop by forcing each repetition
          //  to be at least one frame apart.
          #ifdef MGLSC_DEBUG_LOG
          #ifdef MGLSC_DEBUG_PROGKEY
            fprintf( mglclient.dbfp, "KEYDEF %d - [%d]: REPEAT\n",
             mglclient.activeKeyDef, mglclient.keyDefInstruction );
            fflush( mglclient.dbfp );
          #endif
          #endif
          mglclient.keyDefInstruction = 0;
          tmp = 0;
        break;
        case KEYDEF_WAIT:
          mglclient.keyDefWaitCount++;
          if ( mglclient.keyDefWaitCount >
                KEYDEF_PARAM( keyDefs[ mglclient.activeKeyDef ].
                 instructions[ mglclient.keyDefInstruction ] ) )
          {
            #ifdef MGLSC_DEBUG_LOG
            #ifdef MGLSC_DEBUG_PROGKEY
              fprintf( mglclient.dbfp, "KEYDEF %d - [%d]: WAIT %d\n",
               mglclient.activeKeyDef, mglclient.keyDefInstruction,
               KEYDEF_PARAM( keyDefs[ mglclient.activeKeyDef ].
                instructions[ mglclient.keyDefInstruction ] ) );
              fflush( mglclient.dbfp );
            #endif
            #endif
            mglclient.keyDefInstruction++;
            mglclient.keyDefWaitCount = 0;
          }
          tmp = 0;
        break;
      }
      if ( mglclient.keyDefInstruction >
            keyDefs[ mglclient.activeKeyDef ].numInstructions )
      {
        #ifdef MGLSC_DEBUG_LOG
        #ifdef MGLSC_DEBUG_PROGKEY
          fprintf( mglclient.dbfp, "KEYDEF END: %d\n",
           mglclient.activeKeyDef );
          fflush( mglclient.dbfp );
        #endif
        #endif
        mglclient.activeKeyDef = 0;
        mglclient.keyDefInstruction = 0;
        mglclient.keyDefWaitCount = 0;
        tmp = 0;
      }
    }
  }

  if ( !mglclient.timer0 )
  {
    pollstarttime += polltime;

    pollleftovers += polladder;
    polltime = pollbase;

    tmp = (ULONG)pollleftovers;
    if ( tmp )
    {
      // in other words, if it's 1 or higher
      pollleftovers -= tmp;
      polltime += tmp;
      // every millisecond counts!  ;-)
    }
  }

  if ( skipped )
  {
    mglclient.curskip++;
  } else {
    mglclient.curskip = 0;
  }

  mglclient.laggedby--;

  if ( mglclient.timer0 )
  {
    DosResetEventSem( mglclient.timer_tick_sem, &postcount );
    mglclient.laggedby += postcount;
  }

  if ( mglclient.laggedby < 0 )
  {
    mglclient.laggedby = 0;
  }
}

void MGLSC_resetSkipParams( void )
{
  mglclient.curskip = 0;
  mglclient.laggedby = 0;
}

void MGLSC_shutdown( void )
{
  WinDestroyWindow( mglclient.framewin );
  DosWaitThread( &(mglclient.pm_thread), DCWW_WAIT );
  // Sit and wait for the PM thread to end

  memset( &mglclient, 0, sizeof( mglclient ) );
}

void MGLSC_waitForShutdown( void )
{
  DosWaitThread( &(mglclient.pm_thread), DCWW_WAIT );
  // Sit and wait for the PM thread to end

  memset( &mglclient, 0, sizeof( mglclient ) );
}

static HMODULE getDLLmodHandle( void )
{
  HMODULE ret;
  
  if ( DosQueryModuleHandle( "DualMode.DLL", &ret ) )
  {
    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "Unable to query DualMode.DLL module handle.  Can't load dialog resource.\n" );
      fflush( mglclient.dbfp );
    #endif
    return NULLHANDLE;
  }
  
  return ret;
}

static unsigned char keySelected = 0;

void CenterWindow( HWND win )
{
  RECTL rectl, rectl2;
  WinQueryWindowRect( HWND_DESKTOP, &rectl );
  WinQueryWindowRect( win, &rectl2 );
  WinSetWindowPos( win, 0, (rectl.xRight -
   (rectl2.xRight-rectl2.xLeft)-rectl.xLeft) / 2, 
   (rectl.yTop-(rectl2.yTop-rectl2.yBottom)-rectl.yBottom) / 2,
   0, 0, SWP_MOVE );
}

#define KEYPROC_KEYSELECTED WM_USER + 100

MRESULT EXPENTRY customizeDialogProc( HWND win, ULONG msg, MPARAM mp1,
 MPARAM mp2 )
{
  static char pressedKeySelected = 0;
  
  switch ( msg )
  {
    case WM_INITDLG:
    {
      char text[512] = { 0 };
      int i, inst;
      
      pressedKeySelected = 0;
      
      CenterWindow( win );
      strcpy( text, "Key Definition: " );
      WinQueryWindowText(
       WinWindowFromID( mglclient.keyDialog, keySelected + 100 ),
       496, &(text[16]) );
      WinSetWindowText( WinWindowFromID( win, FID_TITLEBAR ), text );
      WinCheckButton( win, RepeatCheckbox, FALSE );
      
      WinSendDlgItemMsg( win, WaitFrames, SPBM_SETLIMITS, MPFROMLONG( 120 ),
       MPFROMLONG( 1 ) );
      
      inst = 0;
      
      for ( i=0; i<keyDefs[keySelected].numInstructions; ++i )
      {
        switch ( KEYDEF_INSTR( keyDefs[keySelected].instructions[i] ) )
        {
          case KEYDEF_PRESS:
            strcpy( text, "Press " );
            pressedKeySelected =
             KEYDEF_PARAM( keyDefs[keySelected].instructions[i] );
            WinQueryWindowText( 
             WinWindowFromID( mglclient.keyDialog,
              KEYDEF_PARAM( keyDefs[keySelected].instructions[i] ) + 100 ),
              506, &(text[6]) );
            WinSendDlgItemMsg( win, InstructionList, LM_INSERTITEM,
             MPFROMSHORT( LIT_END ), MPFROMP( text ) );
            WinSendDlgItemMsg( win, InstructionList, LM_SETITEMHANDLE,
             MPFROMSHORT( inst ),
             MPFROMLONG( (ULONG) keyDefs[keySelected].instructions[i] ) );
            inst++;
          break;
          case KEYDEF_RELEASE:
            strcpy( text, "Release " );
            WinQueryWindowText(
             WinWindowFromID( mglclient.keyDialog,
              KEYDEF_PARAM( keyDefs[keySelected].instructions[i] ) + 100 ),
              504, &(text[8]) );
            WinSendDlgItemMsg( win, InstructionList, LM_INSERTITEM,
             MPFROMSHORT( LIT_END ), MPFROMP( text ) );
            WinSendDlgItemMsg( win, InstructionList, LM_SETITEMHANDLE,
             MPFROMSHORT( inst ),
             MPFROMLONG( (ULONG) keyDefs[keySelected].instructions[i] ) );
            inst++;
          break;
          case KEYDEF_REPEAT:
            WinCheckButton( win, RepeatCheckbox, TRUE );
          break;
          case KEYDEF_WAIT:
            sprintf( text, "Wait %d frames",
             KEYDEF_PARAM( keyDefs[keySelected].instructions[i] ) );
            WinSendDlgItemMsg( win, InstructionList, LM_INSERTITEM,
             MPFROMSHORT( LIT_END ), MPFROMP( text ) );
            WinSendDlgItemMsg( win, InstructionList, LM_SETITEMHANDLE,
             MPFROMSHORT( inst ),
             MPFROMLONG( (ULONG) keyDefs[keySelected].instructions[i] ) );
            inst++;
          break;
          default:
            sprintf( text, "UNKNOWN INSTRUCTION!: %d",
             KEYDEF_INSTR( keyDefs[keySelected].instructions[i] ) );
            WinSendDlgItemMsg( win, InstructionList, LM_INSERTITEM,
             MPFROMSHORT( LIT_END ), MPFROMP( text ) );
            WinSendDlgItemMsg( win, InstructionList, LM_SETITEMHANDLE,
             MPFROMSHORT( inst ),
             MPFROMLONG( (ULONG) keyDefs[keySelected].instructions[i] ) );
            inst++;
        }
      }
      
      if ( keyDefs[keySelected].numInstructions )
      {
        WinSendDlgItemMsg( win, InstructionList, LM_SELECTITEM,
         MPFROMSHORT( 0 ), MPFROMSHORT( TRUE ) );
      } else {
        WinCheckButton( win, PressRadio, TRUE );
        WinSendMsg( win, WM_CONTROL, MPFROM2SHORT( PressRadio, BN_CLICKED ),
         MPFROMLONG( WinWindowFromID( win, PressRadio ) ) );
      }
    }
    break;
    case WM_COMMAND:
      switch ( SHORT1FROMMP( mp1 ) )
      {
        case SelectButton:
        {
          char text[512] = { 0 };
          strcpy( text, "Key Definition: " );
          WinQueryWindowText(
           WinWindowFromID( mglclient.keyDialog, keySelected + 100 ),
           496, &(text[16]) );
          strcat( text, " - Which key do you want to press?" );
          WinSetDlgItemText( mglclient.keyDialog, KeyboardText, text );
          WinShowWindow( mglclient.keyDialog, TRUE );
          WinSetFocus( HWND_DESKTOP, mglclient.keyDialog );
          return NULL;
        }
        break;
        case AddButton:
        {
          char text[ 128 ];
          SHORT idx;
          USHORT instruction = 0;
          char valid = 0;
          
          idx = SHORT1FROMMR( WinSendDlgItemMsg( win, InstructionList,
           LM_QUERYSELECTION, MPFROMSHORT( LIT_FIRST ), 0 ) );
          
          if ( idx == LIT_NONE ) idx = -1;
          
          if ( WinQueryButtonCheckstate( win, PressRadio ) )
          {
            SHORT i;
            char pressedToggle = 0;
            
            if ( !pressedKeySelected )
            {
              WinAlarm( HWND_DESKTOP, WA_WARNING );
              return 0;
            }
            
            for ( i=0; i<=idx; ++i )
            {
              instruction = SHORT1FROMMR( WinSendDlgItemMsg( win,
               InstructionList, LM_QUERYITEMHANDLE, MPFROMSHORT( i ), 0 ) );
              if ( KEYDEF_INSTR( instruction ) == KEYDEF_PRESS &&
                   KEYDEF_PARAM( instruction ) == pressedKeySelected )
              {
                pressedToggle = 1;
              } else if ( KEYDEF_INSTR( instruction ) == KEYDEF_RELEASE &&
                          KEYDEF_PARAM( instruction ) == pressedKeySelected )
              {
                pressedToggle = 0;
              }
            }
            
            if ( pressedToggle )
            {
              // Already pressed
              WinAlarm( HWND_DESKTOP, WA_WARNING );
              return 0;
            }
            
            instruction = KEYDEF_MAKEINSTR( KEYDEF_PRESS, pressedKeySelected );
            
            strcpy( text, "Press " );
            WinQueryWindowText( 
             WinWindowFromID( mglclient.keyDialog, pressedKeySelected + 100 ),
             506, &(text[6]) );
            valid = 1;
          } else if ( WinQueryButtonCheckstate( win, ReleaseRadio ) )
          {
            SHORT idx2;
            USHORT curInstr;
            LONG keySelected;
            
            idx2 = SHORT1FROMMR( WinSendDlgItemMsg( win, ReleaseDropDown,
             LM_QUERYSELECTION, MPFROMSHORT( LIT_FIRST ), 0 ) );
            
            if ( idx2 == LIT_NONE )
            {
              WinAlarm( HWND_DESKTOP, WA_WARNING );
              return 0;
            }
            
            keySelected = LONGFROMMR( WinSendDlgItemMsg( win, ReleaseDropDown,
             LM_QUERYITEMHANDLE, MPFROMSHORT( idx2 ), 0 ) );
            
            instruction = KEYDEF_MAKEINSTR( KEYDEF_RELEASE, keySelected );

            if ( idx != -1 )
            {
              curInstr = SHORT1FROMMR( WinSendDlgItemMsg( win, InstructionList,
               LM_QUERYITEMHANDLE, MPFROMSHORT( idx ), 0 ) );
              if ( curInstr == instruction )
              {
                WinAlarm( HWND_DESKTOP, WA_WARNING );
                return 0;
              }
            }
            
            strcpy( text, "Release " );
            WinQueryWindowText( 
             WinWindowFromID( mglclient.keyDialog, keySelected + 100),
             504, &(text[8]) );
            valid = 1;
          } else if ( WinQueryButtonCheckstate( win, WaitRadio ) )
          {
            LONG value;
            
            WinSendDlgItemMsg( win, WaitFrames, SPBM_QUERYVALUE,
             MPFROMP( &value ), MPFROM2SHORT( 0, SPBQ_ALWAYSUPDATE ) );
            
            instruction = KEYDEF_MAKEINSTR( KEYDEF_WAIT, value );
             
            sprintf( text, "Wait %ld frames", value );
            valid = 1;
          }
          
          if ( valid && instruction )
          {
            WinSendDlgItemMsg( win, InstructionList, LM_INSERTITEM,
             MPFROMSHORT( idx + 1 ), MPFROMP( text ) );
            WinSendDlgItemMsg( win, InstructionList, LM_SETITEMHANDLE,
             MPFROMSHORT( idx + 1 ), MPFROMLONG( (ULONG) instruction ) );
            WinSendDlgItemMsg( win, InstructionList, LM_SELECTITEM,
             MPFROMSHORT( idx + 1 ), MPFROMSHORT( TRUE ) );
          }
          return 0;
        }
        break;
        case DeleteButton:
        {
          SHORT idx, maxIdx;
          
          idx = SHORT1FROMMR( WinSendDlgItemMsg( win, InstructionList,
           LM_QUERYSELECTION, MPFROMSHORT( LIT_FIRST ), 0 ) );
          
          if ( idx == LIT_NONE )
          {
            WinAlarm( HWND_DESKTOP, WA_WARNING );
            return 0;
          }
          
          WinSendDlgItemMsg( win, InstructionList, LM_DELETEITEM,
           MPFROMSHORT( idx ), 0 );
          maxIdx = SHORT1FROMMR( WinSendDlgItemMsg( win, InstructionList,
           LM_QUERYITEMCOUNT, 0, 0 ) );
          if ( idx > maxIdx )
          {
            idx = maxIdx;
          }
          WinSendDlgItemMsg( win, InstructionList, LM_SELECTITEM,
           MPFROMSHORT( idx ), MPFROMSHORT( TRUE ) );
          return 0;
        }
        break;
        case DID_OK:
        {
          SHORT numInstr, i;
          int repeatActive;
          
          numInstr = SHORT1FROMMR( WinSendDlgItemMsg( win, InstructionList,
           LM_QUERYITEMCOUNT, 0, 0 ) );
          
          repeatActive = WinQueryButtonCheckstate( win, RepeatCheckbox );
           
          if ( repeatActive )
          {
            numInstr++;
          }

          if ( keyDefs[keySelected].numInstructions != numInstr )
          {
            free( keyDefs[keySelected].instructions );
            if ( !numInstr )
            {
              keyDefs[keySelected].instructions = NULL;
              keyDefs[keySelected].numInstructions = 0;
            } else {
              keyDefs[keySelected].instructions = (unsigned short *)
               malloc( numInstr * sizeof( unsigned short ) );
              keyDefs[keySelected].numInstructions = numInstr;
            }
          }
          
          if ( repeatActive )
          {
            numInstr--;
          }
          
          for ( i=0; i<numInstr; ++i )
          {
            keyDefs[keySelected].instructions[i] = SHORT1FROMMR(
             WinSendDlgItemMsg( win, InstructionList, LM_QUERYITEMHANDLE,
              MPFROMSHORT( i ), 0 ) );
          }
          
          if ( repeatActive )
          {
            keyDefs[keySelected].instructions[numInstr] =
             KEYDEF_MAKEINSTR( KEYDEF_REPEAT, 0 );
          }
        }
        
        // Intentional fall-through
        
        default:
          WinDestroyWindow( mglclient.keyDialog );
          WinDestroyWindow( mglclient.keyCustomizeDialog );
          mglclient.keyDialog = 0;
          mglclient.keyCustomizeDialog = 0;
      }
    break;
    case WM_CONTROL:
      if ( SHORT2FROMMP( mp1 ) == LN_SELECT &&
           SHORT1FROMMP( mp1 ) == InstructionList )
      {
        USHORT instruction, inst2, inst3;
        SHORT idx, i, j, numPressed;
        char text[64], releasedAlready, curInstructionFound;
        
        idx = SHORT1FROMMR( WinSendDlgItemMsg( win, InstructionList,
         LM_QUERYSELECTION, MPFROMSHORT( LIT_FIRST ), 0 ) );
         
        if ( idx == LIT_NONE )
        {
          WinSetDlgItemText( win, KeySelectedText, "No key selected" );
          WinSendDlgItemMsg( win, ReleaseDropDown, LM_DELETEALL, 0, 0 );
          WinSendDlgItemMsg( win, WaitFrames, SPBM_SETCURRENTVALUE,
           MPFROMLONG( 1 ), 0 );
          break;
        }

        instruction = SHORT1FROMMR(
         WinSendDlgItemMsg( win, InstructionList, LM_QUERYITEMHANDLE,
          MPFROMSHORT( idx ), 0 ) );
        
        WinSendDlgItemMsg( win, ReleaseDropDown, LM_DELETEALL, 0, 0 );
        numPressed = 0;
        curInstructionFound = 0;
        for ( i=0; i<=idx; ++i )
        {
          inst2 = SHORT1FROMMR(
           WinSendDlgItemMsg( win, InstructionList, LM_QUERYITEMHANDLE,
            MPFROMSHORT( i ), 0 ) );
          if ( KEYDEF_INSTR( inst2 ) == KEYDEF_PRESS )
          {
            releasedAlready = 0;
            
            for ( j=i+1; j<idx; ++j )
            {
              inst3 = SHORT1FROMMR(
               WinSendDlgItemMsg( win, InstructionList, LM_QUERYITEMHANDLE,
                MPFROMSHORT( j ), 0 ) );
              if ( KEYDEF_INSTR( inst3 ) == KEYDEF_RELEASE &&
                   KEYDEF_PARAM( inst3 ) == KEYDEF_PARAM( inst2 ) )
              {
                releasedAlready = 1;
                break;
              }
            }
            
            if ( releasedAlready )
            {
              continue;
            }
            
            WinQueryWindowText(
             WinWindowFromID( mglclient.keyDialog,
              KEYDEF_PARAM( inst2 ) + 100 ), 64, text );
            WinSendDlgItemMsg( win, ReleaseDropDown, LM_INSERTITEM,
             MPFROMSHORT( LIT_END ), MPFROMP( text ) );
            WinSendDlgItemMsg( win, ReleaseDropDown, LM_SETITEMHANDLE,
             MPFROMSHORT( numPressed ),
             MPFROMSHORT( KEYDEF_PARAM( inst2 ) ) );
                
            if ( KEYDEF_INSTR( instruction ) == KEYDEF_RELEASE &&
                 KEYDEF_PARAM( inst2 ) == KEYDEF_PARAM( instruction ) )
            {
              WinSendDlgItemMsg( win, ReleaseDropDown, LM_SELECTITEM,
               MPFROMSHORT( numPressed ), MPFROMSHORT( TRUE ) );
              curInstructionFound = 1;
            }
            
            numPressed++;
          }
        }
        
        if ( !curInstructionFound )
        {
          if ( SHORT1FROMMR( WinSendDlgItemMsg( win, ReleaseDropDown,
                LM_QUERYITEMCOUNT, 0, 0 ) ) )
          {
            WinSendDlgItemMsg( win, ReleaseDropDown, LM_SELECTITEM,
             MPFROMSHORT( 0 ), MPFROMSHORT( TRUE ) );
          } else {
            WinSendDlgItemMsg( win, ReleaseDropDown, EM_CLEAR, 0, 0 );
          }
        }
        
        if ( KEYDEF_INSTR( instruction ) != KEYDEF_WAIT )
        {
          WinSendDlgItemMsg( win, WaitFrames, SPBM_SETCURRENTVALUE,
           MPFROMLONG( 1 ), 0 );
        }

        switch ( KEYDEF_INSTR( instruction ) )
        {
          case KEYDEF_PRESS:
            WinCheckButton( win, PressRadio, TRUE );
            WinSendMsg( win, WM_CONTROL,
             MPFROM2SHORT( PressRadio, BN_CLICKED ),
             MPFROMLONG( WinWindowFromID( win, PressRadio ) ) );
            WinQueryWindowText(
             WinWindowFromID( mglclient.keyDialog,
              KEYDEF_PARAM( instruction ) + 100 ), 64, text );
            WinSetDlgItemText( win, KeySelectedText, text );
          break;
          case KEYDEF_RELEASE:
            WinCheckButton( win, ReleaseRadio, TRUE );
            WinSendMsg( win, WM_CONTROL,
             MPFROM2SHORT( ReleaseRadio, BN_CLICKED ),
             MPFROMLONG( WinWindowFromID( win, ReleaseRadio ) ) );
          break;
          case KEYDEF_WAIT:
            WinCheckButton( win, WaitRadio, TRUE );
            WinSendMsg( win, WM_CONTROL,
             MPFROM2SHORT( WaitRadio, BN_CLICKED ),
             MPFROMLONG( WinWindowFromID( win, WaitRadio ) ) );
            WinSendDlgItemMsg( win, WaitFrames, SPBM_SETCURRENTVALUE,
             MPFROMLONG( KEYDEF_PARAM( instruction ) ), 0 );
          break;
        }
      }
      
      if ( SHORT2FROMMP( mp1 ) != BN_CLICKED &&
           SHORT2FROMMP( mp1 ) != BN_DBLCLICKED )
      {
        return WinDefDlgProc( win, msg, mp1, mp2 );
      }
      
      switch ( SHORT1FROMMP( mp1 ) )
      {
        case PressRadio:
          WinEnableWindow( WinWindowFromID( win, KeySelectedText ), TRUE );
          WinEnableWindow( WinWindowFromID( win, SelectButton ), TRUE );
          WinEnableWindow( WinWindowFromID( win, ReleaseDropDown ), FALSE );
          WinEnableWindow( WinWindowFromID( win, WaitFrames ), FALSE );
          WinEnableWindow( WinWindowFromID( win, FramesText ), FALSE );
        break;
        case ReleaseRadio:
          WinEnableWindow( WinWindowFromID( win, KeySelectedText ), FALSE );
          WinEnableWindow( WinWindowFromID( win, SelectButton ), FALSE );
          WinEnableWindow( WinWindowFromID( win, ReleaseDropDown ), TRUE );
          WinEnableWindow( WinWindowFromID( win, WaitFrames ), FALSE );
          WinEnableWindow( WinWindowFromID( win, FramesText ), FALSE );
        break;
        case WaitRadio:
          WinEnableWindow( WinWindowFromID( win, KeySelectedText ), FALSE );
          WinEnableWindow( WinWindowFromID( win, SelectButton ), FALSE );
          WinEnableWindow( WinWindowFromID( win, ReleaseDropDown ), FALSE );
          WinEnableWindow( WinWindowFromID( win, WaitFrames ), TRUE );
          WinEnableWindow( WinWindowFromID( win, FramesText ), TRUE );
        break;
      }
    break;
    case KEYPROC_KEYSELECTED:
    {
      char text[64] = { 0 };
      WinQueryWindowText(
       WinWindowFromID( mglclient.keyDialog, SHORT1FROMMP(mp1) + 100 ),
       64, text );
      WinSetDlgItemText( win, KeySelectedText, text );
      pressedKeySelected = (char) SHORT1FROMMP(mp1);
    }
    break;
  }
  return WinDefDlgProc( win, msg, mp1, mp2 );
}

MRESULT EXPENTRY keyboardDialogProc( HWND win, ULONG msg, MPARAM mp1,
 MPARAM mp2 )
{
  switch ( msg )
  {
    case WM_INITDLG:
    {
      CenterWindow( win );
    }
    break;
    case WM_COMMAND:
    {
      HMODULE thisDLL;
      
      if ( SHORT1FROMMP( mp1 ) == DID_CANCEL )
      {
        if ( !mglclient.keyCustomizeDialog )
        {
          mglclient.keyDialog = NULLHANDLE;
        }
        break;
      }
      
      thisDLL = getDLLmodHandle();
      if ( mglclient.keyCustomizeDialog )
      {
        WinSendMsg( mglclient.keyCustomizeDialog, KEYPROC_KEYSELECTED,
         MPFROMSHORT( SHORT1FROMMP(mp1) ) - 100, NULL );
        WinSetFocus( HWND_DESKTOP, mglclient.keyCustomizeDialog );
        return WinDefDlgProc( win, msg, mp1, mp2 );
      }
      
      keySelected = SHORT1FROMMP(mp1) - 100;
      mglclient.keyCustomizeDialog = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP,
       customizeDialogProc, thisDLL, 2, NULL );
      
      if ( mglclient.keyDialog == NULLHANDLE )
      {
        #ifdef MGLSC_DEBUG_LOG
          fprintf( mglclient.dbfp, "Failed to load dialog resource from DLL.\n" );
          fflush( mglclient.dbfp );
        #endif
      }
    }
    break;
    case WM_CLOSE:
      if ( !mglclient.keyCustomizeDialog ) mglclient.keyDialog = NULLHANDLE;
    break;
  }
  return WinDefDlgProc( win, msg, mp1, mp2 );
}

void MGLSC_customizeKeyboard( void )
{
  HMODULE thisDLL;
  
  thisDLL = getDLLmodHandle();
  
  if ( mglclient.keyDialog )
  {
    WinAlarm( HWND_DESKTOP, WA_WARNING );
    return;
  }
  
  mglclient.keyDialog = WinLoadDlg( HWND_DESKTOP, HWND_DESKTOP,
   keyboardDialogProc, thisDLL, 1, NULL );
  
  if ( mglclient.keyDialog == NULLHANDLE )
  {
    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "Failed to load dialog resource from DLL.\n" );
      fflush( mglclient.dbfp );
    #endif
    return;
  }
}

static int increasePos( int *curPos, int maxPos )
{
  (*curPos)++;
  if ( *curPos > maxPos )
  {
    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp,
       "Unexpected end of key definition data in INI.\n" );
      fflush( mglclient.dbfp );
    #endif
    return 1;
  }
  return 0;
}

void MGLSC_loadCustomKeyboard( char *iniName, char *appName, char *keyName )
{
  HINI iniFile = PrfOpenProfile( WinQueryAnchorBlock( mglclient.framewin ),
   (PUCHAR) iniName );
  ULONG keyOptionsSize = 0;
  int curPos = 0, numInstructions, i;
  char *keyOptionsRaw, theKey;
  USHORT instruction;
  
  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Loading keyboard customizations from INI.\n" );
    fflush( mglclient.dbfp );
  #endif
  
  if ( iniFile == NULLHANDLE )
  {
    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "Unable to open INI file: %s\n", iniName );
      fflush( mglclient.dbfp );
    #endif
    return;
  }
  
  PrfQueryProfileSize( iniFile, appName, keyName, &keyOptionsSize );
  if ( !keyOptionsSize ) return;
  
  keyOptionsRaw = (char *) malloc( keyOptionsSize );
  PrfQueryProfileData( iniFile, appName, keyName, keyOptionsRaw,
   &keyOptionsSize );
  
  while ( curPos < keyOptionsSize )
  {
    theKey = keyOptionsRaw[ curPos ];
    if ( theKey > 105 )
    {
      #ifdef MGLSC_DEBUG_LOG
        fprintf( mglclient.dbfp, "Unknown key (%d) specified in keyboard definition.\nRest of definition ignored.\n",
         theKey );
        fflush( mglclient.dbfp );
      #endif
      break;
    }
    
    if ( increasePos( &curPos, keyOptionsSize ) ) break;
    numInstructions = keyOptionsRaw[ curPos ];
    if ( increasePos( &curPos, keyOptionsSize ) ) break;
    
    if ( keyDefs[theKey].numInstructions )
    {
      free( keyDefs[theKey].instructions );
    }
    
    keyDefs[theKey].instructions = (USHORT *)
     malloc( numInstructions * sizeof( USHORT ) );
    keyDefs[theKey].numInstructions = numInstructions;
    
    for ( i=0; i<numInstructions; ++i )
    {
      instruction = *((USHORT *)&(keyOptionsRaw[ curPos ]));
      if ( increasePos( &curPos, keyOptionsSize ) ) break;
      if ( increasePos( &curPos, keyOptionsSize ) ) break;
      keyDefs[theKey].instructions[i] = instruction;
    }
  }
  
  PrfCloseProfile( iniFile );
}

void MGLSC_saveCustomKeyboard( char *iniName, char *appName, char *keyName )
{
  HINI iniFile = PrfOpenProfile( WinQueryAnchorBlock( mglclient.framewin ),
   (PUCHAR) iniName );
  int totalBytes, i, j, curPos;
  char *tempBuffer;
  
  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Saving keyboard customizations to INI.\n" );
    fflush( mglclient.dbfp );
  #endif
  
  if ( iniFile == NULLHANDLE )
  {
    #ifdef MGLSC_DEBUG_LOG
      fprintf( mglclient.dbfp, "Unable to open INI file: %s\n", iniName );
      fflush( mglclient.dbfp );
    #endif
    return;
  }
  
  totalBytes = 0;
  
  for ( i=0; i<106; ++i )
  {
    if ( keyDefs[i].numInstructions )
    {
      totalBytes += 2 + (keyDefs[i].numInstructions * 2);
    }
  }
  
  if ( !totalBytes )
  {
    PrfWriteProfileData( iniFile, appName, keyName, NULL, 0 );
    PrfCloseProfile( iniFile );
    return;
  }
  tempBuffer = (char *) malloc( totalBytes );
  curPos = 0;
  
  for ( i=0; i<106; ++i )
  {
    if ( keyDefs[i].numInstructions )
    {
      tempBuffer[ curPos ] = i;
      tempBuffer[ curPos + 1 ] = keyDefs[i].numInstructions;
      curPos += 2;
      
      for ( j=0; j<keyDefs[i].numInstructions; ++j )
      {
        *((USHORT *)(&(tempBuffer[ curPos ]))) = keyDefs[i].instructions[j];
        curPos += 2;
      }
    }
  }
  
  PrfWriteProfileData( iniFile, appName, keyName, tempBuffer, totalBytes );
  PrfCloseProfile( iniFile );
  
  free( tempBuffer );

  #ifdef MGLSC_DEBUG_LOG
    fprintf( mglclient.dbfp, "Keyboard customizations saved to INI successfully.\n" );
    fflush( mglclient.dbfp );
  #endif
}



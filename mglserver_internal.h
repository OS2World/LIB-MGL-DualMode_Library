// IMPORTANT NOTE:
// Make sure you compiler uses "packed" structures.  If the compiler pads
// the structures below out to a certain size, then there's a chance that it
// won't work with existing DLLs and executables.

#ifndef MGL_SERVER_INTERNAL_H
#define MGL_SERVER_INTERNAL_H

#define INCL_DOS
#include <os2.h>
#include <mgraph.h>

#define MGLS_COMMAND_QUEUE_NAME (PUCHAR)"\\QUEUES\\MGLServer Command Queue"

//
// The following are messages sent to the server command queue.
//
#define MGLS_INIT               0
// Initialize MGL library, but don't kick into a graphics mode
#define MGLS_SHUTDOWN           1
// Shutdown and cleanup MGL library
#define MGLS_INIT_VIDMODE       2
// Set or change the video mode
#define MGLS_SHUTDOWN_VIDMODE   3
// Return to the default (text) mode
#define MGLS_INIT_BUFFER        4
// Create an MGL memory device context for an allocated buffer
#define MGLS_FREE_BUFFER        5
// Delete the memory device context for an allocated buffer
#define MGLS_BLIT_BUFFER        6
// Blit the buffer to the hardware device context
#define MGLS_STRETCHBLIT_BUFFER 7
// Blit the buffer and stretch it
#define MGLS_EVENT_FLUSH        8
// Process MGL events (keyboard & mouse)
#define MGLS_SET_COLORS         9
// Set the physical color palette
#define MGLS_SET_MOUSE_POS      10
// Set the mouse pointer location

//
// The following flags are for use with the MGLS_INIT_VIDMODE message
//
#define MGLS_VMIFLAG_USE_CUSTOM 1
// (Input) - allow custom modes to be used
#define MGLS_VMOFLAG_SUCCESS    1
// (Output) - mode was successfully selected


//
// The following are messages sent to the client listener queue.
//
#define MGLC_VIDEO_SWITCH_NOTIFICATION  1
// Data of 0 indicates that we are returning to the PM session
// Data of 1 indicates that we are going to the full screen session
#define MGLC_KEYDOWN_NOTIFICATION       2
// Data value indicates the scan code of the key pressed
#define MGLC_KEYUP_NOTIFICATION         3
// Data value indicates the scan code of the key released
#define MGLC_MOUSEMOVE_NOTIFICATION     4
// Low 16 bits of data value are the new absolute X position
// High 16 bits of data value are the new absolute Y position
#define MGLC_MOUSEBUTTON_NOTIFICATION   5
// Data value indicates which mouse buttons are pressed


#define MGLS_BIGGEST_PACKET_SIZE sizeof( MGL_SERVER_MOUSE_POS_PACKET )

//
// The following structures are for use with the corresponding MGL
// command queue messages described above.
//
typedef struct _MGL_SERVER_INIT_PACKET
{
        HQUEUE client_queue;            // OUTPUT - not to be used by client
        HEV client_wakeup;              // OUTPUT - not to be used by client
        char client_semaphore_name[50]; // INPUT
        char input_queue_name[50];      // INPUT
} MGL_SERVER_INIT_PACKET;

typedef struct _MGL_SERVER_INIT_VIDMODE_PACKET
{
        HQUEUE client_queue;            // INPUT - obtained by previous server call
        HEV client_wakeup;              // INPUT - obtained by previous server call
        int width, height, depth;       // INPUT
        int flags;                      // INPUT / OUTPUT
} MGL_SERVER_INIT_VIDMODE_PACKET;

typedef struct _MGL_SERVER_SHUTDOWN_VIDMODE_PACKET
{
        HQUEUE client_queue;            // INPUT - obtained by previous server call
        HEV client_wakeup;              // INPUT - obtained by previous server call
} MGL_SERVER_SHUTDOWN_VIDMODE_PACKET;

typedef struct _MGL_SERVER_INIT_BUFFER_PACKET
{
        HQUEUE client_queue;            // INPUT - obtained by previous server call
        HEV client_wakeup;              // INPUT - obtained by previous server call
        MGLDC *mdc;                     // OUTPUT - not to be used by client
        void *buffer;                   // INPUT
        int width, height, depth;       // INPUT
} MGL_SERVER_INIT_BUFFER_PACKET;

typedef struct _MGL_SERVER_FREE_BUFFER_PACKET
{
        HQUEUE client_queue;            // INPUT - obtained by previous server call
        HEV client_wakeup;              // INPUT - obtained by previous server call
        MGLDC *mdc;                     // INPUT - obtained by previous server call
} MGL_SERVER_FREE_BUFFER_PACKET;

typedef struct _MGL_SERVER_BLIT_BUFFER_PACKET
{
        HQUEUE client_queue;            // INPUT - obtained by previous server call
        HEV client_wakeup;              // INPUT - obtained by previous server call
        MGLDC *mdc;                     // INPUT - obtained by previous server call
        int left, top, right, bottom;   // INPUT
        int destx, desty;               // INPUT
} MGL_SERVER_BLIT_BUFFER_PACKET;

typedef struct _MGL_SERVER_STRETCHBLIT_BUFFER_PACKET
{
        HQUEUE client_queue;            // INPUT - obtained by previous server call
        HEV client_wakeup;              // INPUT - obtained by previous server call
        MGLDC *mdc;                     // INPUT - obtained by previous server call
        int left, top, right, bottom;   // INPUT
        int destleft, desttop;          // INPUT
        int destright, destbottom;      // INPUT
} MGL_SERVER_STRETCHBLIT_BUFFER_PACKET;

typedef struct _MGL_PALETTE_ENTRY
{
        unsigned char blue, green, red, alpha;
} MGL_PALETTE_ENTRY;

typedef struct _MGL_SERVER_COLORS_SET_PACKET
{
        HQUEUE client_queue;            // INPUT - obtained by previous server call
        HEV client_wakeup;              // INPUT - obtained by previous server call
        int reserved[9];                // RESERVED
        MGL_PALETTE_ENTRY colors[256];  // INPUT
} MGL_SERVER_COLORS_SET_PACKET;

typedef struct _MGL_SERVER_MOUSE_POS_PACKET
{
        HQUEUE client_queue;            // INPUT - obtained by previous server call
        HEV client_wakeup;              // INPUT - obtained by previous server call
        int reserved[1033];             // RESERVED
        int newx, newy;                 // INPUT
} MGL_SERVER_MOUSE_POS_PACKET;

typedef struct _MGL_SERVER_SHUTDOWN_PACKET
{
        HQUEUE client_queue;            // INPUT
        HEV client_wakeup;              // INPUT
} MGL_SERVER_SHUTDOWN_PACKET;

#endif


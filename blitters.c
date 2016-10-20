#define INCL_WIN
#include <os2.h>

#include "blitters.h"
#include "dualmode.h"
#include "mglserver_internal.h"

extern struct MGLSC_state mglclient;

static RECTL junkrectl = { 0, 0, 0, 0 };

#define maker565( r, g, b ) (unsigned short) (((unsigned short)(b)>>3) | ((unsigned short)(g)&0x00fc)<<3 | ((unsigned short)(r)&0x00f8)<<8)
#define makergb4( r, g, b ) (unsigned long) (((unsigned long)(r)) | ((unsigned long)(g)<<8) | ((unsigned long)(b)<<16))

#define redcomp32( data32 )   ((data32) & 255)
#define greencomp32( data32 ) ((data32 >> 8) & 255)
#define bluecomp32( data32 )  ((data32 >> 16) & 255)

#define redcomp16( data16 )   ((data16 >> 8) & 248)
#define greencomp16( data16 ) ((data16 >> 3) & 252)
#define bluecomp16( data16 )  ((data16 << 3) & 248)

// Byte 1 -> bits 11->16
// Byte 2 -> bits 5->11
// Byte 3 -> bits 0->5
#define conv32to16lo(c) (((c) & 0xf8) << 8 | ((c) & 0xfc00) >> 5 | ((c) & 0xf80000) >> 19)
#define conv32to16hi(c) (((c) & 0xf8) << 24 | ((c) & 0xfc00) << 11 | ((c) & 0xf80000) >> 3)

// Byte 1 -> bits 0->5
// Byte 2 -> bits 5->11
// Byte 3 -> bits 11->16
#define conv32Bto16lo(c) (((c) & 0xf8) >> 3 | ((c) & 0xfc00) >> 5 | ((c) & 0xf80000) >> 8)
#define conv32Bto16hi(c) (((c) & 0xf8) << 13 | ((c) & 0xfc00) << 9 | ((c) & 0xf80000) << 8)

extern void *linebuffer;
// in Dualmode.c

extern int numregions;
extern blitterRegion *regions;

//
// Uncomment the following line to print the BIC16 compiled code in the log
// when custom paired-16 blitters are used.
//
// #define DEBUG_BIC16

#define DECLARE_VARS( DESTTYPE, SRCTYPE )                     \
  unsigned DESTTYPE *framebuf, *screenbuf;                    \
  unsigned SRCTYPE *srcbuf = mglclient.vidbuffer, *basestart; \
                                                              \
  int i;                                                      \
  register int j;                                             \
                                                              \
  int srcstartx, srcstopx, deststartx, deststopx;             \
  int srcstarty, srcstopy, deststarty, deststopy;             \
  int destwidth, destheight;                                  \
  int srcwidth, sofar;                                        \
  double multx, multy;

#define GENERIC_CLIPPING_SCALING( SRCMULT ) \
  multx = (float) (regions[region].view_x2 - regions[region].view_x1 + 1) / \
   (float) regions[region].winsizex; \
  multy = (float) (regions[region].view_y2 - regions[region].view_y1 + 1) / \
   (float) regions[region].winsizey; \
  \
  if ( regions[region].winposx < 0 ) \
  { \
    srcstartx  = regions[region].view_x1 + \
     ((-regions[region].winposx) * multx); \
    deststartx = 0; \
  } else { \
    srcstartx  = regions[region].view_x1; \
    deststartx = regions[region].winposx; \
  } \
  \
  if ( (regions[region].winposx + regions[region].winsizex) > \
        mglclient.desktopscan ) \
  { \
    srcstopx  = regions[region].view_x1 + \
     ((mglclient.desktopscan - regions[region].winposx) * multx); \
    deststopx = mglclient.desktopscan; \
  } else { \
    srcstopx  = regions[region].view_x2; \
    deststopx = regions[region].winposx + regions[region].winsizex; \
  } \
  \
  if ( regions[region].winposy < 0 ) \
  { \
    srcstarty  = regions[region].view_y1 + \
     ((-regions[region].winposy) * multy); \
    deststarty = 0; \
  } else { \
    srcstarty  = regions[region].view_y1; \
    deststarty = regions[region].winposy; \
  } \
  \
  if ( (regions[region].winposy + regions[region].winsizey) > \
        mglclient.desktopheight ) \
  { \
    srcstopy = regions[region].view_y1 + \
     ((mglclient.desktopheight - regions[region].winposy) * multy); \
    deststopy = mglclient.desktopheight; \
  } else { \
    srcstopy = regions[region].view_y2; \
    deststopy = regions[region].winposy + regions[region].winsizey; \
  } \
  \
  destwidth  = deststopx - deststartx; \
  destheight = deststopy - deststarty; \
  srcwidth  = srcstopx - srcstartx + 1; \
  \
  basestart = srcbuf + (srcstarty * mglclient.width * SRCMULT) + \
   (srcstartx * SRCMULT); \
  srcbuf = basestart;

#define BLITTER_LOOPS( DESTMULT, DESTTYPE )                      \
  sofar = 0;                                                     \
  DiveAcquireFrameBuffer( mglclient.diveinst, &junkrectl );      \
  screenbuf = (unsigned DESTTYPE *)mglclient.PMframebuffer +     \
   (((deststarty * mglclient.desktopscan) + deststartx)          \
    * DESTMULT);                                                 \
  for ( i=0; i<destheight && sofar <= srcstopy; )                \
  {                                                              \
    if ( regions[region].offsetmapy[i] )                         \
    {                                                            \
      framebuf = screenbuf;                                      \
    } else {                                                     \
      framebuf = linebuffer;                                     \
    }                                                            \
                                                                 \
    for ( j=0; j<destwidth; ++j )                                \
    {

#define END_BLITTER_LOOPS( DESTMULT, SRCMULT )        \
      framebuf += DESTMULT;                           \
      srcbuf += regions[region].offsetmapx[j] * SRCMULT; \
    }                                                 \
    if ( !regions[region].offsetmapy[i] )             \
    {                                                 \
      do                                              \
      {                                               \
        if ( i >= destheight ) { break; }             \
        memcpy( screenbuf, linebuffer, destwidth *    \
         DESTMULT * sizeof( screenbuf[0] ) );         \
        screenbuf += mglclient.desktopscan;           \
      } while ( !regions[region].offsetmapy[i++] );   \
      --i;                                            \
    } else {                                          \
      screenbuf += mglclient.desktopscan;             \
    }                                                 \
    sofar += regions[region].offsetmapy[i];           \
    srcbuf = basestart +                              \
     (sofar * mglclient.width * SRCMULT);             \
    ++i;                                              \
  }                                                   \
  DiveDeacquireFrameBuffer( mglclient.diveinst );

#define PAIRED16_BLITTER_LOOPS                                   \
  sofar = 0;                                                     \
  DiveAcquireFrameBuffer( mglclient.diveinst, &junkrectl );      \
  screenbuf = (unsigned long *)mglclient.PMframebuffer +         \
   (((deststarty * mglclient.desktopscan) + deststartx) / 2);    \
  for ( i=0; i<destheight && sofar <= srcstopy; )                \
  {                                                              \
    if ( regions[region].offsetmapy[i] )                         \
    {                                                            \
      framebuf = screenbuf;                                      \
    } else {                                                     \
      framebuf = linebuffer;                                     \
    }                                                            \
                                                                 \
    for ( j=1; j<regions[region].offsetmapx[0]; ++j )            \
    {

#define END_PAIRED16_BLITTER_LOOPS( SRCMULT )           \
      framebuf++;                                       \
    }                                                   \
    if ( !regions[region].offsetmapy[i] )               \
    {                                                   \
      do                                                \
      {                                                 \
        if ( i > destheight ) break;                    \
        memcpy( screenbuf, linebuffer, destwidth * 2 ); \
        screenbuf += mglclient.desktopscan / 2;         \
      } while ( !regions[region].offsetmapy[i++] );     \
      --i;                                              \
    } else {                                            \
      screenbuf += mglclient.desktopscan / 2;           \
    }                                                   \
    sofar += regions[region].offsetmapy[i];             \
    srcbuf = basestart +                                \
     (sofar * mglclient.width * SRCMULT);               \
    ++i;                                                \
  }                                                     \
  DiveDeacquireFrameBuffer( mglclient.diveinst );

//
// Blitter instruction codes for 16-bit pixel-pairing
//
// Lower 8 bits are the instruction code.
// Rest of the 24 bits are the parameters.
// (12 bits for first instruction, 12 for the second)
// Instructions are bitmapped, using the BIC16 masks.
// Low order 4 bits represent pixel 1 of the pair, high order are for pixel 2.
// Only SkipN takes a parameter.
// A Fetch is implicit after a SkipN.
//
#define BIC16_NOOP  0
#define BIC16_FETCH 1
#define BIC16_CLONE 2
#define BIC16_LEAVE 4
#define BIC16_SKIPN 8

#define BIC16_FETCH_FETCH (1 | (1 << 4))
#define BIC16_FETCH_CLONE (1 | (2 << 4))
#define BIC16_FETCH_LEAVE (1 | (4 << 4))
#define BIC16_FETCH_SKIPN (1 | (8 << 4))
#define BIC16_CLONE_FETCH (2 | (1 << 4))
#define BIC16_CLONE_CLONE (2 | (2 << 4))
#define BIC16_CLONE_LEAVE (2 | (4 << 4))
#define BIC16_LEAVE_FETCH (4 | (1 << 4))
#define BIC16_SKIPN_FETCH (8 | (1 << 4))
#define BIC16_SKIPN_LEAVE (8 | (4 << 4))
#define BIC16_SKIPN_SKIPN (8 | (8 << 4))

#define SETPIX( instruction ) \
  if ( whichpix ) \
  { \
    tmpinstr |= instruction << 4; \
    regions[region].offsetmapx[ip*2] = tmpinstr; \
    ip++; \
    whichpix = 0; \
  } else { tmpinstr = instruction;  whichpix = 1; }

#define SETPIXWITHPARM( instruction, parm ) \
  if ( whichpix ) \
  { \
    tmpinstr |= instruction << 4; \
    regions[region].offsetmapx[ip*2] = tmpinstr; \
    regions[region].offsetmapx[(ip*2)+1] = \
     (regions[region].offsetmapx[(ip*2)+1] & 0xffff) | parm << 16; \
    ip++; \
    whichpix = 0; \
  } else { \
    tmpinstr = instruction; \
    regions[region].offsetmapx[(ip*2)+1] = parm; \
    whichpix = 1; \
  }

void DirectBlit8To8( int region )
{
  DECLARE_VARS( char, char );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 1, char )
   *framebuf = *srcbuf;
  END_BLITTER_LOOPS( 1, 1 )
}

#define convPalTo16( col ) maker565( cs->colors[col].red, cs->colors[col].green, cs->colors[col].blue )

void DirectBlit8To16( int region )
{
  MGL_SERVER_COLORS_SET_PACKET *cs = 
   (MGL_SERVER_COLORS_SET_PACKET *) mglclient.shared_packet;

  register unsigned long thepix = 0;

  DECLARE_VARS( long, char );

  GENERIC_CLIPPING_SCALING( 1 );

  PAIRED16_BLITTER_LOOPS
    if ( regions[region].offsetmapx[j*2] & BIC16_FETCH )
    {
      if ( regions[region].offsetmapx[j*2] & (BIC16_FETCH << 4) )
      {
        thepix = convPalTo16( *srcbuf ) | (convPalTo16( srcbuf[1] ) << 16);
        srcbuf += 2;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_CLONE << 4) )
      {
        thepix = convPalTo16( *srcbuf ) | (convPalTo16( *srcbuf ) << 16);
        ++srcbuf;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_SKIPN << 4) )
      {
        thepix  = convPalTo16( *srcbuf );
        srcbuf += (regions[region].offsetmapx[(j*2)+1] >> 16) + 2;
        thepix |= convPalTo16( srcbuf[-1] ) << 16;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_LEAVE << 4) ) 
      {
        // Since it's a "LEAVE" on the end, this is the end of the line,
        // so don't worry about adding 1 more for the fetch.
        thepix = convPalTo16( *srcbuf ) | ((*framebuf) & 0xffff0000);
        *(framebuf++) = thepix;
        continue;
      }
    } else if ( regions[region].offsetmapx[j*2] & BIC16_CLONE )
    {
      if ( regions[region].offsetmapx[j*2] & (BIC16_CLONE << 4) )
      {
        thepix &= 0xffff0000;
        thepix |= thepix >> 16;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_FETCH << 4) )
      {
        thepix = (thepix >> 16) | (convPalTo16( *srcbuf ) << 16);
        ++srcbuf;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_LEAVE << 4) )
      {
        // Since it's a "LEAVE" on the end, this is the end of the line,
        // so don't worry about adding 1 more for the fetch.
        thepix = (thepix >> 16) | ((*framebuf) &0xffff0000);
        *(framebuf++) = thepix;
        continue;
      }
    } else if ( regions[region].offsetmapx[j*2] & BIC16_SKIPN )
    {
      if ( regions[region].offsetmapx[j*2] & (BIC16_FETCH << 4) )
      {
        srcbuf += (regions[region].offsetmapx[(j*2)+1] & 0xffff) + 2;
        thepix  = convPalTo16( srcbuf[-2] ) | (convPalTo16( srcbuf[-1] ) << 16);
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_SKIPN << 4) )
      {
        srcbuf += (regions[region].offsetmapx[(j*2)+1] & 0xffff) + 1;
        thepix  = convPalTo16( srcbuf[-1] );
        srcbuf += (regions[region].offsetmapx[(j*2)+1] >> 16) + 1;
        thepix |= convPalTo16( srcbuf[-1] ) << 16;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_LEAVE << 4) )
      {
        // Since it's a "LEAVE" on the end, this is the end of the line,
        // so don't worry about adding 1 more for the fetch.
        thepix  = convPalTo16( srcbuf[ regions[region].offsetmapx[(j*2)+1]
         & 0xffff ] ) | ((*framebuf) & 0xffff0000);
        *(framebuf++) = thepix;
        continue;
      }
    } else if ( regions[region].offsetmapx[j*2] & BIC16_LEAVE )
    {
      thepix  = ((*framebuf) & 0xffff) | (convPalTo16( *srcbuf ) << 16);
      ++srcbuf;
      *(framebuf++) = thepix;
      continue;
    }
  END_PAIRED16_BLITTER_LOOPS( 1 );
}

void DirectBlit8To24( int region )
{
  MGL_SERVER_COLORS_SET_PACKET *cs = 
   (MGL_SERVER_COLORS_SET_PACKET *) mglclient.shared_packet;

  DECLARE_VARS( char, char );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 3, char )
    *framebuf     = cs->colors[*srcbuf].red;
    *(framebuf+1) = cs->colors[*srcbuf].green;
    *(framebuf+2) = cs->colors[*srcbuf].blue;
  END_BLITTER_LOOPS( 3, 1 )
}

void DirectBlit8To24B( int region )
{
  MGL_SERVER_COLORS_SET_PACKET *cs = 
   (MGL_SERVER_COLORS_SET_PACKET *) mglclient.shared_packet;

  DECLARE_VARS( char, char );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 3, char )
    *framebuf     = cs->colors[*srcbuf].blue;
    *(framebuf+1) = cs->colors[*srcbuf].green;
    *(framebuf+2) = cs->colors[*srcbuf].red;
  END_BLITTER_LOOPS( 3, 1 )
}

void DirectBlit8To32( int region )
{
  MGL_SERVER_COLORS_SET_PACKET *cs = 
   (MGL_SERVER_COLORS_SET_PACKET *) mglclient.shared_packet;

  DECLARE_VARS( long, char );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 1, long )
    *framebuf =
     makergb4( cs->colors[*srcbuf].red, cs->colors[*srcbuf].green,
      cs->colors[*srcbuf].blue );
  END_BLITTER_LOOPS( 1, 1 )
}

void DirectBlit8To32B( int region )
{
  MGL_SERVER_COLORS_SET_PACKET *cs = 
   (MGL_SERVER_COLORS_SET_PACKET *) mglclient.shared_packet;

  DECLARE_VARS( long, char );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 1, long )
    *framebuf =
     makergb4( cs->colors[*srcbuf].blue, cs->colors[*srcbuf].green,
      cs->colors[*srcbuf].red );
  END_BLITTER_LOOPS( 1, 1 )
}

void DirectBlit16To8( int region )
{
  // NOT SUPPORTED.  JUST PAINT THE WINDOW WITH COLOR 0.

  DECLARE_VARS( char, short );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 1, char )
    *framebuf = 0;
  END_BLITTER_LOOPS( 1, 1 )
}

void DirectBlit16To16( int region )
{
  register unsigned long thepix = 0;

  DECLARE_VARS( long, short );

  GENERIC_CLIPPING_SCALING( 1 );

  PAIRED16_BLITTER_LOOPS
    if ( regions[region].offsetmapx[j*2] & BIC16_FETCH )
    {
      if ( regions[region].offsetmapx[j*2] & (BIC16_FETCH << 4) )
      {
        thepix = *((unsigned long *)srcbuf);
        srcbuf += 2;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_CLONE << 4) )
      {
        thepix = *srcbuf | (*srcbuf << 16);
        ++srcbuf;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_SKIPN << 4) )
      {
        thepix  = *srcbuf;
        srcbuf += (regions[region].offsetmapx[(j*2)+1] >> 16) + 2;
        thepix |= srcbuf[-1] << 16;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_LEAVE << 4) ) 
      {
        // Since it's a "LEAVE" on the end, this is the end of the line,
        // so don't worry about adding 1 more for the fetch.
        thepix = *srcbuf | ((*framebuf) & 0xffff0000);
        *(framebuf++) = thepix;
        continue;
      }
    } else if ( regions[region].offsetmapx[j*2] & BIC16_CLONE )
    {
      if ( regions[region].offsetmapx[j*2] & (BIC16_CLONE << 4) )
      {
        thepix &= 0xffff0000;
        thepix |= thepix >> 16;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_FETCH << 4) )
      {
        thepix = (thepix >> 16) | (*srcbuf << 16);
        ++srcbuf;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_LEAVE << 4) )
      {
        // Since it's a "LEAVE" on the end, this is the end of the line,
        // so don't worry about adding 1 more for the fetch.
        thepix = (thepix >> 16) | ((*framebuf) & 0xffff0000);
        *(framebuf++) = thepix;
        continue;
      }
    } else if ( regions[region].offsetmapx[j*2] & BIC16_SKIPN )
    {
      if ( regions[region].offsetmapx[j*2] & (BIC16_FETCH << 4) )
      {
        srcbuf += (regions[region].offsetmapx[(j*2)+1] & 0xffff) + 2;
        thepix  = srcbuf[-2] | (srcbuf[-1] << 16);
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_SKIPN << 4) )
      {
        srcbuf += (regions[region].offsetmapx[(j*2)+1] & 0xffff) + 1;
        thepix  = srcbuf[-1];
        srcbuf += (regions[region].offsetmapx[(j*2)+1] >> 16) + 1;
        thepix |= (srcbuf[-1] << 16);
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_LEAVE << 4) )
      {
        // Since it's a "LEAVE" on the end, this is the end of the line,
        // so don't worry about adding 1 more for the fetch.
        thepix  = srcbuf[ regions[region].offsetmapx[(j*2)+1] & 0xffff ] |
                   ((*framebuf) & 0xffff0000);
        *(framebuf++) = thepix;
        continue;
      }
    } else if ( regions[region].offsetmapx[j*2] & BIC16_LEAVE )
    {
      thepix  = ((*framebuf) & 0xffff) | (*srcbuf << 16);
      ++srcbuf;
      *(framebuf++) = thepix;
      continue;
    }
  END_PAIRED16_BLITTER_LOOPS( 1 );
}

void DirectBlit16To24( int region )
{
  DECLARE_VARS( char, short );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 3, char )
    *((unsigned short*)framebuf) = redcomp16( *srcbuf ) |
     (greencomp16( *srcbuf ) << 16);
    *(framebuf + 2) = bluecomp16( *srcbuf );
  END_BLITTER_LOOPS( 3, 1 )
}

void DirectBlit16To24B( int region )
{
  DECLARE_VARS( char, short );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 3, char )
    *((unsigned short*)framebuf) = bluecomp16( *srcbuf ) |
     (greencomp16( *srcbuf ) << 16);
    *(framebuf + 2) = redcomp16( *srcbuf );
  END_BLITTER_LOOPS( 3, 1 )
}

void DirectBlit16To32( int region )
{
  DECLARE_VARS( long, short );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 1, long )
    *framebuf = makergb4( redcomp16( *srcbuf ), greencomp16( *srcbuf ),
     bluecomp16( *srcbuf ) );
  END_BLITTER_LOOPS( 1, 1 )
}

void DirectBlit16To32B( int region )
{
  DECLARE_VARS( long, short );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 1, long )
    *framebuf = makergb4( bluecomp16( *srcbuf ), greencomp16( *srcbuf ),
     redcomp16( *srcbuf ) );
  END_BLITTER_LOOPS( 1, 1 )
}

void DirectBlit24To8( int region )
{
  // NOT SUPPORTED.  JUST PAINT THE WINDOW WITH COLOR 0.

  DECLARE_VARS( char, char );

  GENERIC_CLIPPING_SCALING( 3 );

  BLITTER_LOOPS( 1, char )
    *framebuf = 0;
  END_BLITTER_LOOPS( 1, 3 )
}

void DirectBlit24To16( int region )
{
  DECLARE_VARS( short, char );

  GENERIC_CLIPPING_SCALING( 3 );

  BLITTER_LOOPS( 1, short )
    *framebuf =
     maker565( *srcbuf, *(srcbuf + 1), *(srcbuf + 2) );
  END_BLITTER_LOOPS( 1, 3 )
}

void DirectBlit24BTo16( int region )
{
  DECLARE_VARS( short, char );

  GENERIC_CLIPPING_SCALING( 3 );

  BLITTER_LOOPS( 1, short )
    *framebuf =
     maker565( *(srcbuf + 2), *(srcbuf + 1), *srcbuf );
  END_BLITTER_LOOPS( 1, 3 )
}

void DirectBlit24To24( int region )
{
  DECLARE_VARS( char, char );

  GENERIC_CLIPPING_SCALING( 3 );

  BLITTER_LOOPS( 3, char )
    *((unsigned short *)framebuf) = *((unsigned short *)srcbuf);
    *(framebuf + 2) = *(srcbuf + 2);
  END_BLITTER_LOOPS( 3, 3 )
}

void DirectBlit24To24B( int region )
{
  DECLARE_VARS( char, char );

  GENERIC_CLIPPING_SCALING( 3 );

  BLITTER_LOOPS( 3, char )
    *((unsigned short *)framebuf) = ((unsigned char *)srcbuf)[2] |
     (((unsigned char *)srcbuf)[1] << 8);
    *(framebuf + 2) = *srcbuf;
  END_BLITTER_LOOPS( 3, 3 )
}

void DirectBlit24To32( int region )
{
  DECLARE_VARS( long, char );

  GENERIC_CLIPPING_SCALING( 3 );

  BLITTER_LOOPS( 1, long )
    *framebuf = makergb4( *srcbuf, *(srcbuf+1), *(srcbuf+2) );
  END_BLITTER_LOOPS( 1, 3 )
}

void DirectBlit24To32B( int region )
{
  DECLARE_VARS( long, char );

  GENERIC_CLIPPING_SCALING( 3 );

  BLITTER_LOOPS( 1, long )
    *framebuf = makergb4( *(srcbuf+2), *(srcbuf+1), *srcbuf );
  END_BLITTER_LOOPS( 1, 3 )
}

void DirectBlit32To8( int region )
{
  // NOT SUPPORTED.  JUST PAINT THE WINDOW WITH COLOR 0.

  DECLARE_VARS( char, long );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 1, char )
    *framebuf = 0;
  END_BLITTER_LOOPS( 1, 1 )
}

void DirectBlit32To16( int region )
{
  register unsigned long thepix = 0;

  DECLARE_VARS( long, long );

  GENERIC_CLIPPING_SCALING( 1 );

  PAIRED16_BLITTER_LOOPS
    if ( regions[region].offsetmapx[j*2] & BIC16_FETCH )
    {
      if ( regions[region].offsetmapx[j*2] & (BIC16_FETCH << 4) )
      {
        thepix = conv32to16lo( *srcbuf ) | conv32to16hi( srcbuf[1] );
        srcbuf += 2;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_CLONE << 4) )
      {
        thepix = conv32to16lo( *srcbuf ) | conv32to16hi( *srcbuf );
        ++srcbuf;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_SKIPN << 4) )
      {
        thepix  = conv32to16lo( *srcbuf );
        srcbuf += (regions[region].offsetmapx[(j*2)+1] >> 16) + 2;
        thepix |= conv32to16hi( srcbuf[-1] );
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_LEAVE << 4) ) 
      {
        // Since it's a "LEAVE" on the end, this is the end of the line,
        // so don't worry about adding 1 more for the fetch.
        thepix = conv32to16lo( *srcbuf ) | ((*framebuf) & 0xffff0000);
        *(framebuf++) = thepix;
        continue;
      }
    } else if ( regions[region].offsetmapx[j*2] & BIC16_CLONE )
    {
      if ( regions[region].offsetmapx[j*2] & (BIC16_CLONE << 4) )
      {
        thepix &= 0xffff0000;
        thepix |= thepix >> 16;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_FETCH << 4) )
      {
        thepix = (thepix >> 16) | conv32to16hi( *srcbuf );
        ++srcbuf;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_LEAVE << 4) )
      {
        // Since it's a "LEAVE" on the end, this is the end of the line,
        // so don't worry about adding 1 more for the fetch.
        thepix = (thepix >> 16) | ((*framebuf) & 0xffff0000);
        *(framebuf++) = thepix;
        continue;
      }
    } else if ( regions[region].offsetmapx[j*2] & BIC16_SKIPN )
    {
      if ( regions[region].offsetmapx[j*2] & (BIC16_FETCH << 4) )
      {
        srcbuf += (regions[region].offsetmapx[(j*2)+1] & 0xffff) + 2;
        thepix  = conv32to16lo( srcbuf[-2] ) | conv32to16hi( srcbuf[-1] );
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_SKIPN << 4) )
      {
        srcbuf += (regions[region].offsetmapx[(j*2)+1] & 0xffff) + 1;
        thepix  = conv32to16lo( srcbuf[-1] );
        srcbuf += (regions[region].offsetmapx[(j*2)+1] >> 16) + 1;
        thepix |= conv32to16hi( srcbuf[-1] );
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_LEAVE << 4) )
      {
        // Since it's a "LEAVE" on the end, this is the end of the line,
        // so don't worry about adding 1 more for the fetch.
        thepix  = conv32to16lo( srcbuf[ regions[region].offsetmapx[(j*2)+1]
         & 0xffff ] ) | ((*framebuf) & 0xffff0000);
        *(framebuf++) = thepix;
        continue;
      }
    } else if ( regions[region].offsetmapx[j*2] & BIC16_LEAVE )
    {
      thepix  = ((*framebuf) & 0xffff) | conv32to16hi( *srcbuf );
      ++srcbuf;
      *(framebuf++) = thepix;
      continue;
    }
  END_PAIRED16_BLITTER_LOOPS( 1 );
}

void DirectBlit32BTo16( int region )
{
  register unsigned long thepix = 0;

  DECLARE_VARS( long, long );

  GENERIC_CLIPPING_SCALING( 1 );

  PAIRED16_BLITTER_LOOPS
    if ( regions[region].offsetmapx[j*2] & BIC16_FETCH )
    {
      if ( regions[region].offsetmapx[j*2] & (BIC16_FETCH << 4) )
      {
        thepix = conv32Bto16lo( *srcbuf ) | conv32Bto16hi( srcbuf[1] );
        srcbuf += 2;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_CLONE << 4) )
      {
        thepix = conv32Bto16lo( *srcbuf ) | conv32Bto16hi( *srcbuf );
        ++srcbuf;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_SKIPN << 4) )
      {
        thepix  = conv32Bto16lo( *srcbuf );
        srcbuf += (regions[region].offsetmapx[(j*2)+1] >> 16) + 2;
        thepix |= conv32Bto16hi( srcbuf[-1] );
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_LEAVE << 4) ) 
      {
        // Since it's a "LEAVE" on the end, this is the end of the line,
        // so don't worry about adding 1 more for the fetch.
        thepix = conv32Bto16lo( *srcbuf ) | ((*framebuf) & 0xffff0000);
        *(framebuf++) = thepix;
        continue;
      }
    } else if ( regions[region].offsetmapx[j*2] & BIC16_CLONE )
    {
      if ( regions[region].offsetmapx[j*2] & (BIC16_CLONE << 4) )
      {
        thepix &= 0xffff0000;
        thepix |= thepix >> 16;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_FETCH << 4) )
      {
        thepix = (thepix >> 16) | conv32Bto16hi( *srcbuf );
        ++srcbuf;
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_LEAVE << 4) )
      {
        // Since it's a "LEAVE" on the end, this is the end of the line,
        // so don't worry about adding 1 more for the fetch.
        thepix = (thepix >> 16) | ((*framebuf) & 0xffff0000);
        *(framebuf++) = thepix;
        continue;
      }
    } else if ( regions[region].offsetmapx[j*2] & BIC16_SKIPN )
    {
      if ( regions[region].offsetmapx[j*2] & (BIC16_FETCH << 4) )
      {
        srcbuf += (regions[region].offsetmapx[(j*2)+1] & 0xffff) + 2;
        thepix  = conv32Bto16lo( srcbuf[-2] ) | conv32Bto16hi( srcbuf[-1] );
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_SKIPN << 4) )
      {
        srcbuf += (regions[region].offsetmapx[(j*2)+1] & 0xffff) + 1;
        thepix  = conv32Bto16lo( srcbuf[-1] );
        srcbuf += (regions[region].offsetmapx[(j*2)+1] >> 16) + 1;
        thepix |= conv32Bto16hi( srcbuf[-1] );
        *(framebuf++) = thepix;
        continue;
      }
      if ( regions[region].offsetmapx[j*2] & (BIC16_LEAVE << 4) )
      {
        // Since it's a "LEAVE" on the end, this is the end of the line,
        // so don't worry about adding 1 more for the fetch.
        thepix  = conv32Bto16lo( srcbuf[ regions[region].offsetmapx[(j*2)+1]
         & 0xffff ] ) | ((*framebuf) & 0xffff0000);
        *(framebuf++) = thepix;
        continue;
      }
    } else if ( regions[region].offsetmapx[j*2] & BIC16_LEAVE )
    {
      thepix  = ((*framebuf) & 0xffff) | conv32Bto16hi( *srcbuf );
      ++srcbuf;
      *(framebuf++) = thepix;
      continue;
    }
  END_PAIRED16_BLITTER_LOOPS( 1 );
}

void DirectBlit32To24( int region )
{
  DECLARE_VARS( char, long );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 3, char )
    *((unsigned short *)framebuf) = *((unsigned short *)srcbuf);
    *(framebuf+2) = bluecomp32( *srcbuf );
  END_BLITTER_LOOPS( 3, 1 )
}

void DirectBlit32To24B( int region )
{
  DECLARE_VARS( char, long );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 3, char )
    *((unsigned short *)framebuf) = ((unsigned char *)srcbuf)[2] |
     (((unsigned char *)srcbuf)[1]<<8);
    *(framebuf+2) = ((unsigned char *)srcbuf)[0];
  END_BLITTER_LOOPS( 3, 1 )
}

void DirectBlit32To32( int region )
{
  DECLARE_VARS( long, long );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 1, long )
    *framebuf = *srcbuf;
  END_BLITTER_LOOPS( 1, 1 )
}

void DirectBlit32To32B( int region )
{
  DECLARE_VARS( long, long );

  GENERIC_CLIPPING_SCALING( 1 );

  BLITTER_LOOPS( 1, long )
    *framebuf = ((unsigned char *)srcbuf)[2] | (((unsigned char *)srcbuf)[1] << 8) |
     (((unsigned char *)srcbuf)[0] << 16);
  END_BLITTER_LOOPS( 1, 1 )
}

void RemapOffsets( int region )
{
  int i;
  int srcstartx, srcstopx, deststartx, deststopx;
  int srcstarty, srcstopy, deststarty, deststopy;
  int destwidth, destheight;
  int srcwidth, srcheight, sofar;
  double accumulator;
  double multx, multy;

  if ( !regions[region].offsetmapx || !regions[region].offsetmapy ||
       !regions[region].winsizex || !regions[region].winsizey ) return;

  multx = (float) (regions[region].view_x2 - regions[region].view_x1 + 1) /
   (float) regions[region].winsizex;
  multy = (float) (regions[region].view_y2 - regions[region].view_y1 + 1) /
   (float) regions[region].winsizey;

  if ( regions[region].winposx < 0 )
  {
    srcstartx  = regions[region].view_x1 +
     ((-regions[region].winposx) * multx);
    deststartx = 0;
  } else {
    srcstartx  = regions[region].view_x1;
    deststartx = regions[region].winposx;
  }

  if ( (regions[region].winposx + regions[region].winsizex) >
       mglclient.desktopscan )
  {
    srcstopx  = regions[region].view_x1 +
     ((mglclient.desktopscan - regions[region].winposx) * multx);
    deststopx = mglclient.desktopscan;
  } else {
    srcstopx  = regions[region].view_x2;
    deststopx = regions[region].winposx + regions[region].winsizex;
  }

  if ( regions[region].winposy < 0 )
  {
    srcstarty  = regions[region].view_y1 +
     ((-regions[region].winposy) * multy);
    deststarty = 0;
  } else {
    srcstarty  = regions[region].view_y1;
    deststarty = regions[region].winposy;
  }

  if ( (regions[region].winposy + regions[region].winsizey) >
       mglclient.desktopheight )
  {
    srcstopy  = regions[region].view_y1 +
     ((mglclient.desktopheight - regions[region].winposy) * multy);
    deststopy = mglclient.desktopheight;
  } else {
    srcstopy  = regions[region].view_y2;
    deststopy = regions[region].winposy + regions[region].winsizey;
  }

  destwidth  = deststopx - deststartx;
  destheight = deststopy - deststarty;
  srcwidth  = srcstopx - srcstartx;
  srcheight = srcstopy - srcstarty;

  sofar = 0;
  accumulator = 0;

  if ( mglclient.desktopdepth == 16 )
  {
    unsigned long tmpinstr = 0; // temporary instruction
    int ip = 1;                 // instruction pointer
    char whichpix = 0;          // low or high pixel
    int tmpint;

    // Special case so we can do pixel-paired writes to video memory
    // offsetmapx becomes a set of instructions rather than a repeat count
    // We use the BIC16 instruction set here.

    if ( srcstartx & 1 )
    {
      // Starts on second pixel of a pixel pair
      SETPIX( BIC16_LEAVE );
      regions[region].winposx--;
      regions[region].winsizex++;
    }

    SETPIX( BIC16_FETCH );
    accumulator = 0;

    for ( i=1; i < destwidth && sofar < srcwidth; ++i )
    {
      accumulator += multx;
      tmpint = (int)accumulator;
      if ( tmpint == 0 )
      {
        SETPIX( BIC16_CLONE );
      } else if ( tmpint == 1 )
      {
        SETPIX( BIC16_FETCH );
      } else {
        // more than one pixel advance
        if ( sofar + tmpint > srcwidth )
        {
          tmpint = srcwidth - sofar;
        }
        if ( tmpint == 1 )
        {
          SETPIX( BIC16_FETCH );
        } else {
          SETPIXWITHPARM( BIC16_SKIPN, (tmpint - 1) );
        }
      }
      sofar += tmpint;
      accumulator -= tmpint;
    }
    if ( whichpix )
    {
      SETPIX( BIC16_LEAVE );
      regions[region].winsizex++;
    }
    // Pad out the last entry if it's not on an even 32-bit boundary

    regions[region].offsetmapx[0] = ip;
    // Store the number of instructions at [0]

    #ifdef DEBUG_BIC16

    sofar = 0;
    fprintf( mglclient.dbfp, "\nBIC compiled code:\n" );
    for ( ip = 1; ip < regions[region].offsetmapx[0]; ++ip )
    {
      switch ( regions[region].offsetmapx[ip*2] )
      {
        case BIC16_FETCH_FETCH:
          fprintf( mglclient.dbfp, "FETCH/FETCH\n" );
          sofar += 2;
        break;
        case BIC16_FETCH_CLONE:
          fprintf( mglclient.dbfp, "FETCH/CLONE\n" );
          sofar++;
        break;
        case BIC16_FETCH_LEAVE:
          fprintf( mglclient.dbfp, "FETCH/LEAVE\n" );
          sofar++;
        break;
        case BIC16_FETCH_SKIPN:
          fprintf( mglclient.dbfp, "FETCH/SKIPN(%ld)\n",
           regions[region].offsetmapx[(ip*2)+1] >> 16 );
          sofar += 2 + (regions[region].offsetmapx[(ip*2)+1] >> 16);
        break;
        case BIC16_CLONE_FETCH:
          fprintf( mglclient.dbfp, "CLONE/FETCH\n" );
          sofar++;
        break;
        case BIC16_CLONE_CLONE:
          fprintf( mglclient.dbfp, "CLONE/CLONE\n" );
        break;
        case BIC16_CLONE_LEAVE:
          fprintf( mglclient.dbfp, "CLONE/LEAVE\n" );
        break;
        case BIC16_LEAVE_FETCH:
          fprintf( mglclient.dbfp, "LEAVE/FETCH\n" );
          sofar++;
        break;
        case BIC16_SKIPN_FETCH:
          fprintf( mglclient.dbfp, "SKIPN(%ld)/FETCH\n",
           regions[region].offsetmapx[(ip*2)+1] & 0xffff );
          sofar += 2 + (regions[region].offsetmapx[(ip*2)+1] & 0xffff);
        break;
        case BIC16_SKIPN_LEAVE:
          fprintf( mglclient.dbfp, "SKIPN(%ld)/LEAVE\n",
           regions[region].offsetmapx[(ip*2)+1] & 0xffff );
          sofar += 1 + (regions[region].offsetmapx[(ip*2)+1] & 0xffff);
        break;
        case BIC16_SKIPN_SKIPN:
          fprintf( mglclient.dbfp, "SKIPN(%ld)/SKIPN(%ld)\n",
           regions[region].offsetmapx[(ip*2)+1] & 0xffff,
           regions[region].offsetmapx[(ip*2)+1] >> 16 );
          sofar += 2 + (regions[region].offsetmapx[(ip*2)+1] & 0xffff) +
           (regions[region].offsetmapx[(ip*2)+1] >> 16);
        break;
        default:
          fprintf( mglclient.dbfp, "*** UNKNOWN OPCODE! (%ld) ***\n",
           regions[region].offsetmapx[ip*2] );
      }
    }
    fprintf( mglclient.dbfp, "Code summary:\n=> %ld pixels to be drawn\n=> %d source pixels accounted for.\n",
     (regions[region].offsetmapx[0] * 2) - 2, sofar );
    fprintf( mglclient.dbfp, "Destination Region: (%dx%d @ %d,%d)\n",
     regions[region].winsizex, regions[region].winsizey,
     regions[region].winposx, regions[region].winposy );
    fprintf( mglclient.dbfp, "Source Region:      (%d,%d -> %d,%d)\n",
     regions[region].view_x1, regions[region].view_y1,
     regions[region].view_x2, regions[region].view_y2 );

    #endif // DEBUG_BIC16
  } else {
    for ( i=0; i<destwidth && sofar <= srcwidth; ++i )
    {
      accumulator += multx;

      if ( sofar + (int)accumulator > srcwidth )
      {
        regions[region].offsetmapx[i] = srcwidth - sofar;
        break;
      } else {
        regions[region].offsetmapx[i] = (int)accumulator;
      }
      
      sofar += (int)accumulator;
      accumulator -= (int)accumulator;
    }
  }

  sofar = 0;
  accumulator = 0;

  for ( i=0; i<destheight && sofar <= srcheight; ++i )
  {
    accumulator += multy;
    if ( sofar + (int)accumulator > srcheight )
    {
      regions[region].offsetmapy[i] = srcheight - sofar;
      break;
    } else {
      regions[region].offsetmapy[i] = (int)accumulator;
    }
    sofar += (int)accumulator;
    accumulator -= (int)accumulator;
  }
}


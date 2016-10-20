#ifndef MOUSEREGION_H_INCLUDED
#define MOUSEREGION_H_INCLUDED

#define INCL_DOSDEVIOCTL

#define MOU_GETCONSTRAINEDREGION (0x6D)
#define MOU_SETCONSTRAINEDREGION (0x6C)

typedef struct _CONSTRAINEDREGION
{
  USHORT usX0; // upper left corner (inclusive)
  USHORT usY0;
  USHORT usX1; // lower right corner (inclusive)
  USHORT usY1;
} CONSTRAINEDREGION, *PCONSTRAINEDREGION;

inline void getMouseConstrainedRegion( int *x1, int *y1, int *x2, int *y2 )
{
  CONSTRAINEDREGION cr;
  ULONG action, len, rc, junk;
  HFILE hf;
  rc = DosOpen( "MOUSE$", &hf, &junk, 0, 0, OPEN_ACTION_OPEN_IF_EXISTS,
   OPEN_SHARE_DENYNONE | OPEN_ACCESS_READWRITE, NULL );
  if ( rc ) return;
  len = sizeof( CONSTRAINEDREGION );
  action = 0;
  rc = DosDevIOCtl(hf, IOCTL_POINTINGDEVICE, MOU_GETCONSTRAINEDREGION, 0, 0,
   &action, &cr, sizeof( CONSTRAINEDREGION ), &len );
  if ( !rc )
  {
    *x1 = cr.usX0; *y1 = cr.usY0; *x2 = cr.usX1; *y2 = cr.usY1;
  }
  DosClose( hf );
}

inline void setMouseConstrainedRegion( int x1, int y1, int x2, int y2 )
{
  CONSTRAINEDREGION cr;
  ULONG action, len, rc, junk;
  HFILE hf;
  rc = DosOpen( "MOUSE$", &hf, &junk, 0, 0, OPEN_ACTION_OPEN_IF_EXISTS,
   OPEN_SHARE_DENYNONE | OPEN_ACCESS_READWRITE, NULL );
  if ( rc ) return;
  len = sizeof( CONSTRAINEDREGION );
  action = 0;
  cr.usX0 = x1; cr.usY0 = y1; cr.usX1 = x2; cr.usY1 = y2;
  rc = DosDevIOCtl(hf, IOCTL_POINTINGDEVICE, MOU_SETCONSTRAINEDREGION, 0, 0,
   &action, &cr, sizeof( CONSTRAINEDREGION ), &len );
  DosClose( hf );
}

#endif


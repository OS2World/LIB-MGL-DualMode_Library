struct BlitterTypes
{
  char srcFourCC[4], destFourCC[4];
  int  depth, destdepth;
  char supportedByDIVE;
  void (*directBlitter) (int);
};

typedef struct
{
  signed short winposx, winposy;
  unsigned short winsizex, winsizey;
  unsigned short view_x1, view_x2, view_y1, view_y2;
  unsigned long *offsetmapx, *offsetmapy;
} blitterRegion;

void DirectBlit8To8( int );
void DirectBlit8To16( int );
void DirectBlit8To24( int );
void DirectBlit8To24B( int );
void DirectBlit8To32( int );
void DirectBlit8To32B( int );
void DirectBlit16To8( int );
void DirectBlit16To16( int );
void DirectBlit16To24( int );
void DirectBlit16To24B( int );
void DirectBlit16To32( int );
void DirectBlit16To32B( int );
void DirectBlit24To8( int );
void DirectBlit24To16( int );
void DirectBlit24To24( int );
void DirectBlit24To24B( int );
void DirectBlit24To32( int );
void DirectBlit24To32B( int );
void DirectBlit32To8( int );
void DirectBlit32To16( int );
void DirectBlit32To24( int );
void DirectBlit32To24B( int );
void DirectBlit32To32( int );
void DirectBlit32To32B( int );
void DirectBlit24BTo16( int );
void DirectBlit32BTo16( int );


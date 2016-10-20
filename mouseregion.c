#define INCL_DOSDEVIOCTL
#include <os2.h>
#include "mouseregion.h"

int main( int argc, char **argv )
{
  int x, y;
  if ( argc < 3 )
  {
    printf( "Need 2 parameters (X and Y screen size).\n" );
    return -1;
  }
  x = atol( argv[1] ) - 1;
  y = atol( argv[2] ) - 1;
  printf( "Setting mouse allowed region to 0,0 - %d,%d.\n", x, y );
  setMouseConstrainedRegion( 0, 0, x, y );
}


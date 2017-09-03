#include <stdio.h>

void __attribute__ ((constructor)) install()
{
    printf( "injected text\n" );
}

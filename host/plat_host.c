#include "plat.h"

#include <stdio.h>
#include <stdlib.h>

void *plat_alloc_zero(size_t bytes)
{
    return calloc(1, bytes);
}

void plat_free(void *ptr)
{
    free(ptr);
}

void plat_puts(const char *s)
{
    if (s != NULL)
    {
        fputs(s, stdout);
    }
}

#include "plat.h"

#include <stdlib.h>

void *plat_alloc_zero(size_t bytes)
{
    return calloc(1, bytes);
}

void plat_free(void *ptr)
{
    free(ptr);
}

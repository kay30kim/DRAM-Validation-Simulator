// EDK2 has no libc. core includes <string.h>, and the module folder is on
// the include path so this file answers first. Bodies are in plat_uefi.c
#ifndef DRAMTEST_STRING_H
#define DRAMTEST_STRING_H

#include <stddef.h>

void *memset(void *dest, int value, size_t count);
void *memcpy(void *dest, const void *src, size_t count);

#endif

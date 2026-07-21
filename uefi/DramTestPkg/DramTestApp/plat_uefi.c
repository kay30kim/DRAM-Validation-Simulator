// plat.h for UEFI: memory from Boot Services, output to the console.
// This replaces host/plat_host.c when building the .efi
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

#include "../../../core/plat.h"
#include "string.h"

void *plat_alloc_zero(size_t bytes)
{
    VOID *buffer = NULL;

    if (gBS->AllocatePool(EfiBootServicesData, bytes, &buffer) != EFI_SUCCESS)
    {
        return NULL;
    }
    memset(buffer, 0, bytes);
    return buffer;
}

void plat_free(void *ptr)
{
    if (ptr != NULL)
    {
        gBS->FreePool(ptr);
    }
}

// ConOut takes UTF-16 only, so widen char by char. '\n' becomes '\r\n'
void plat_puts(const char *s)
{
    CHAR16 chunk[64];
    UINTN used = 0;

    while (*s != '\0')
    {
        if (used >= 61)
        {
            chunk[used] = L'\0';
            gST->ConOut->OutputString(gST->ConOut, chunk);
            used = 0;
        }
        if (*s == '\n')
        {
            chunk[used++] = L'\r';
        }
        chunk[used++] = (CHAR16)*s++;
    }
    if (used > 0)
    {
        chunk[used] = L'\0';
        gST->ConOut->OutputString(gST->ConOut, chunk);
    }
}

// core pulls in <string.h>; these are the actual bodies
void *memset(void *dest, int value, size_t count)
{
    unsigned char *p = (unsigned char *)dest;

    while (count-- > 0)
    {
        *p++ = (unsigned char)value;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, size_t count)
{
    unsigned char *to = (unsigned char *)dest;
    const unsigned char *from = (const unsigned char *)src;

    while (count-- > 0)
    {
        *to++ = *from++;
    }
    return dest;
}

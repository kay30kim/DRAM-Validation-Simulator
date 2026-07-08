#ifndef PLAT_H
#define PLAT_H

#include <stddef.h>

/*
 * 플랫폼 추상화 계층.
 * core는 OS 함수(calloc/free 등)를 직접 부르지 않고 이 인터페이스만 쓴다.
 * 구현은 빌드 대상에 따라 갈아 끼운다:
 *   - host/plat_host.c : libc (calloc/free)
 *   - uefi/plat_uefi.c : UEFI Boot Services (AllocateZeroPool/FreePool, 예정)
 */
void *plat_alloc_zero(size_t bytes);
void plat_free(void *ptr);
void plat_puts(const char *s);

#endif /* PLAT_H */

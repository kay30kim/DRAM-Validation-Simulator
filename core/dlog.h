#ifndef DLOG_H
#define DLOG_H

/*
 * core 전용 로그 출력.
 * printf를 직접 쓰지 않는 이유: UEFI(EDK2)의 콘솔 출력은 포맷 규약이
 * 달라서(%s 대신 %a 등) printf용 포맷 문자열이 호환되지 않는다.
 * core는 포맷팅을 스스로 하고, 완성된 문자열만 plat_puts()로 내보낸다.
 *
 * 지원 지정자: %% %s %c %d %u %x %X %zu %zx, 0패딩+폭(%08X 등)
 */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void dlog_printf(const char *fmt, ...);

#endif /* DLOG_H */

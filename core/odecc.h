#ifndef ODECC_H
#define ODECC_H

#include <stddef.h>
#include <stdint.h>

/*
 * DDR5 On-Die ECC 기능 모델 (해밍 SEC).
 * 코드워드 = 데이터 128비트(16B) + 패리티 8비트.
 * 근거: datasheet p.3 "On-Die ECC" (128+8 구조는 JESD79-5).
 * 1비트 오류는 정정. 2비트 오류는 오정정되거나 정정 불가로 검출.
 */
#define ODECC_DATA_BYTES 16U

enum
{
    ODECC_OK = 0,
    ODECC_CORRECTED = 1,
    ODECC_UNCORRECTABLE = 2
};

typedef struct OdeccResult
{
    uint32_t bit_index; // 정정된 비트: 0~127 데이터, 128~135 패리티
} OdeccResult;

uint8_t odecc_encode(const uint8_t data[ODECC_DATA_BYTES]);

// 1비트 오류면 data/parity를 제자리에서 정정. 반환값은 위 enum
int odecc_decode(uint8_t data[ODECC_DATA_BYTES], uint8_t *parity,
                 OdeccResult *result);

#endif

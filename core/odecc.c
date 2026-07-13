#include "odecc.h"

/*
 * 해밍 SEC: 비트 위치마다 고유한 8비트 코드를 부여한다.
 * 패리티 비트(128~135)는 2의 거듭제곱 값, 데이터 비트(0~127)는 나머지.
 * 신드롬 = 재계산 패리티 XOR 저장 패리티.
 */

#define ODECC_CODEWORD_BITS (ODECC_DATA_BYTES * 8U + 8U) /* 136 */

static uint8_t g_pos_to_syndrome[ODECC_CODEWORD_BITS];
static uint16_t g_syndrome_to_pos[256]; // syndrome = bit index + 1
static int g_initialized;

static void init_tables(void)
{
    uint32_t value;
    uint32_t index = 0;
    uint32_t i;

    if (g_initialized)
    {
        return;
    }

    for (value = 1; value <= 255U && index < ODECC_DATA_BYTES * 8U; value++)
    {
        if ((value & (value - 1U)) == 0U) // 2^0 ~ 2^7 = 1,2,4,8,16,32,64,128
        {
            continue;
        }
        g_pos_to_syndrome[index] = (uint8_t)value;
        g_syndrome_to_pos[value] = (uint16_t)(index + 1U);
        index++;
    }

    for (i = 0; i < 8U; i++) // 2^0 ~ 2^7 = 1,2,4,8,16,32,64,128
    {
        uint32_t pos = ODECC_DATA_BYTES * 8U + i;

        g_pos_to_syndrome[pos] = (uint8_t)(1U << i);
        g_syndrome_to_pos[1U << i] = (uint16_t)(pos + 1U);
    }

    g_initialized = 1;
}

// 바이트 배열에서 bit번째 자리의 값이 1인가
static int bit_is_set(const uint8_t *data, uint32_t bit)
{
    uint32_t byte_index = bit / 8U;
    uint32_t bit_in_byte = bit % 8U;

    return (data[byte_index] >> bit_in_byte) & 1U;
}

// 바이트 배열에서 bit번째 자리를 반전
static void flip_bit(uint8_t *data, uint32_t bit)
{
    data[bit / 8U] ^= (uint8_t)(1U << (bit % 8U));
}

static uint8_t calc_parity(const uint8_t data[ODECC_DATA_BYTES])
{
    uint8_t syndrome = 0;
    uint32_t bit;

    for (bit = 0; bit < ODECC_DATA_BYTES * 8U; bit++)
    {
        if (bit_is_set(data, bit))
        {
            syndrome ^= g_pos_to_syndrome[bit];
        }
    }

    return syndrome;
}

uint8_t odecc_encode(const uint8_t data[ODECC_DATA_BYTES])
{
    init_tables();
    return calc_parity(data);
}

int odecc_decode(uint8_t data[ODECC_DATA_BYTES], uint8_t *parity,
                 OdeccResult *result)
{
    uint8_t syndrome;
    uint16_t hit;
    uint32_t pos;

    if (data == NULL || parity == NULL)
    {
        return -1;
    }

    init_tables();

    syndrome = (uint8_t)(calc_parity(data) ^ *parity);
    if (syndrome == 0)
    {
        return ODECC_OK;
    }

    hit = g_syndrome_to_pos[syndrome];
    if (hit == 0)
    {
        return ODECC_UNCORRECTABLE;
    }

    pos = (uint32_t)(hit - 1U);
    if (result != NULL)
    {
        result->bit_index = pos;
    }

    if (pos < ODECC_DATA_BYTES * 8U)
    {
        flip_bit(data, pos);
    }
    else
    {
        *parity ^= (uint8_t)(1U << (pos - ODECC_DATA_BYTES * 8U));
    }

    return ODECC_CORRECTED;
}

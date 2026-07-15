#ifndef DRAM_MODEL_H
#define DRAM_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "odecc.h"

#define DRAM_MAX_FAULTS 32U

/*
 * 주소 구성 근거: SK hynix DDR5 SDRAM 3DS RDIMM datasheet
 * (HMCT04/14MEERAxxxN, 16Gb M-die, Rev 1.0) p.4 "Address Table"
 *   - Bank Group : BG0~BG2  -> 8 groups
 *   - Bank       : BA0~BA1  -> 4 banks / group
 *   - Row        : R0~R15   -> 최대 65,536 rows
 *   - Column     : C0~C9    -> 1KB page
 *
 * 주소 비트 배치 (LSB -> MSB): [ COL(10) | BA(2) | BG(3) | ROW ]
 * 연속 접근이 1KB 페이지를 넘는 순간 뱅크/뱅크그룹으로 인터리브되는,
 * 컨트롤러들이 흔히 쓰는 배치를 단순화한 것이다.
 * ROW 비트 수는 호스트 메모리 제약 때문에 할당 크기에 맞춰 축소된다
 * (스펙 상한 16비트).
 */
#define DRAM_SPEC_BG_BITS 3U
#define DRAM_SPEC_BA_BITS 2U
#define DRAM_SPEC_ROW_BITS 16U
#define DRAM_SPEC_COL_BITS 10U

typedef struct DramGeometry
{
    uint32_t bank_groups;     /* 8 (p.4) */
    uint32_t banks_per_group; /* 4 (p.4) */
    uint32_t rows_per_bank;   /* 1 << row_bits (모델링 값) */
    uint32_t row_size_bytes;  /* 1024 = 1KB page (p.4) */

    uint32_t bg_bits;
    uint32_t ba_bits;
    uint32_t row_bits;
    uint32_t col_bits;

    size_t modelled_size_bytes;
} DramGeometry;

typedef struct DramAddress
{
    uint32_t bg;
    uint32_t bank;
    uint32_t row;
    uint32_t column;
} DramAddress;

/* BIT_FLIP: 저장값 1회 반전, 덮어쓰면 사라짐(soft).
 * STUCK_AT_x: 읽을 때마다 해당 비트 강제, 영구(hard). */
typedef enum DramFaultType
{
    DRAM_FAULT_BIT_FLIP = 0,
    DRAM_FAULT_STUCK_AT_0,
    DRAM_FAULT_STUCK_AT_1,
    DRAM_FAULT_TRANSITION_UP,   // 0->1 전이 실패: 1을 써도 0에 머무름
    DRAM_FAULT_TRANSITION_DOWN, // 1->0 전이 실패: 0을 써도 1에 머무름
    DRAM_FAULT_COUPLING_INV     // aggressor 비트가 전이하면 victim 비트가 반전
} DramFaultType;

typedef struct DramFault
{
    int active;
    DramFaultType type;
    uint32_t address;
    uint32_t bit_mask;
    uint32_t victim_address; // COUPLING 전용
    uint32_t victim_mask;    // COUPLING 전용
} DramFault;

typedef struct DramModel
{
    uint8_t *data;
    uint8_t *parity;
    size_t size_bytes;
    DramGeometry geometry;
    DramFault faults[DRAM_MAX_FAULTS];
    size_t fault_count;

    /* ECC transparency: 정정은 읽기에서 투명하게 일어나고 통계로만 보인다 */
    int odecc_enabled;
    size_t ecc_corr_count;
    size_t ecc_uncorr_count;
    uint32_t ecc_last_corr_addr;
} DramModel;

int dram_init(DramModel *dram, size_t size_bytes);
void dram_free(DramModel *dram);
size_t dram_size_bytes(const DramModel *dram);
size_t dram_modelled_size_bytes(const DramModel *dram);
const DramGeometry *dram_geometry(const DramModel *dram);
int dram_is_initialized(const DramModel *dram);

int dram_is_valid_range(const DramModel *dram, uint32_t address, size_t length);
int dram_write32(DramModel *dram, uint32_t address, uint32_t value);
/* 읽기가 ECC 정정 통계를 갱신하므로 const가 아니다 */
int dram_read32(DramModel *dram, uint32_t address, uint32_t *out_value);

int dram_decode_address(const DramModel *dram, uint32_t address, DramAddress *decoded);
int dram_encode_address(const DramModel *dram, const DramAddress *decoded, uint32_t *address);

int dram_inject_bit_flip(DramModel *dram, uint32_t address, uint32_t bit_mask);

/* type은 STUCK_AT_0/1만 허용 */
int dram_add_stuck_fault(DramModel *dram, DramFaultType type,
                         uint32_t address, uint32_t bit_mask);

// 전이 실패 결함 등록 (type은 TRANSITION_UP/DOWN만 허용)
int dram_add_transition_fault(DramModel *dram, DramFaultType type,
                              uint32_t address, uint32_t bit_mask);

// 반전 커플링 결함 등록: aggressor 비트가 쓰기에서 전이하면 victim 비트가 반전
int dram_add_coupling_fault(DramModel *dram,
                            uint32_t aggressor_address, uint32_t aggressor_mask,
                            uint32_t victim_address, uint32_t victim_mask);

// 커버리지 측정 등 "ECC 없는 순수 셀 동작"이 필요할 때 끄고 켠다
void dram_set_odecc_enabled(DramModel *dram, int enabled);

void dram_clear_faults(DramModel *dram);
size_t dram_fault_count(const DramModel *dram);

size_t dram_ecc_correction_count(const DramModel *dram);
size_t dram_ecc_uncorrectable_count(const DramModel *dram);
uint32_t dram_ecc_last_corrected_addr(const DramModel *dram);
void dram_reset_ecc_stats(DramModel *dram);

// ECS(Error Check and Scrub)
typedef void (*DramScrubReportFn)(void *ctx, uint32_t codeword_addr,
                                  uint32_t bit_index, int uncorrectable);
size_t dram_scrub_range(DramModel *dram, uint32_t start, size_t length,
                        DramScrubReportFn report, void *ctx);

#endif

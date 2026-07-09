#ifndef DRAM_MODEL_H
#define DRAM_MODEL_H

#include <stddef.h>
#include <stdint.h>

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

typedef struct DramFault
{
    int active;
    uint32_t address;
    uint32_t bit_mask;
} DramFault;

typedef struct DramModel
{
    uint8_t *data;
    size_t size_bytes;
    DramGeometry geometry;
    DramFault faults[DRAM_MAX_FAULTS];
    size_t fault_count;
} DramModel;

int dram_init(DramModel *dram, size_t size_bytes);
void dram_free(DramModel *dram);
size_t dram_size_bytes(const DramModel *dram);
size_t dram_modelled_size_bytes(const DramModel *dram);
const DramGeometry *dram_geometry(const DramModel *dram);
int dram_is_initialized(const DramModel *dram);

int dram_is_valid_range(const DramModel *dram, uint32_t address, size_t length);
int dram_write32(DramModel *dram, uint32_t address, uint32_t value);
int dram_read32(const DramModel *dram, uint32_t address, uint32_t *out_value);

int dram_decode_address(const DramModel *dram, uint32_t address, DramAddress *decoded);
int dram_encode_address(const DramModel *dram, const DramAddress *decoded, uint32_t *address);

int dram_add_bit_flip_fault(DramModel *dram, uint32_t address, uint32_t bit_mask);
void dram_clear_faults(DramModel *dram);
size_t dram_fault_count(const DramModel *dram);

#endif /* DRAM_MODEL_H */

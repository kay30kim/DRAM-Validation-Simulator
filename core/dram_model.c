#include "dram_model.h"
#include "odecc.h"
#include "plat.h"

#include <string.h>

static int is_aligned32(uint32_t address)
{
    return (address % 4U) == 0U;
}

static int dram_geometry_init(DramGeometry *geometry, size_t size_bytes)
{
    size_t banks_total = 0;
    size_t rows_available = 0;
    uint32_t row_bits = 0;

    if (geometry == NULL || size_bytes == 0)
    {
        return -1;
    }

    memset(geometry, 0, sizeof(*geometry));

    geometry->bg_bits = DRAM_SPEC_BG_BITS;
    geometry->ba_bits = DRAM_SPEC_BA_BITS;
    geometry->col_bits = DRAM_SPEC_COL_BITS;
    geometry->bank_groups = 1U << DRAM_SPEC_BG_BITS;
    geometry->banks_per_group = 1U << DRAM_SPEC_BA_BITS;
    geometry->row_size_bytes = 1U << DRAM_SPEC_COL_BITS;
    banks_total = (size_t)geometry->bank_groups * geometry->banks_per_group;
    rows_available = size_bytes / (banks_total * geometry->row_size_bytes);
    if (rows_available == 0)
    {
        return -1;
    }

    while (row_bits < DRAM_SPEC_ROW_BITS &&
           ((size_t)1 << (row_bits + 1U)) <= rows_available)
    {
        row_bits++;
    }

    geometry->row_bits = row_bits;
    geometry->rows_per_bank = 1U << row_bits;
    geometry->modelled_size_bytes = (size_t)geometry->rows_per_bank *
                                    banks_total *
                                    geometry->row_size_bytes;

    if (geometry->modelled_size_bytes == 0 ||
        geometry->modelled_size_bytes > size_bytes)
    {
        return -1;
    }

    return 0;
}

// apply stuck-at faults to a codeword (data + parity) not directly in dram->data specific address
static void apply_stuck_faults_codeword(const DramModel *dram, uint32_t cw_addr,
                                        uint8_t data[ODECC_DATA_BYTES])
{
    size_t index;

    for (index = 0; index < dram->fault_count; index++)
    {
        const DramFault *fault = &dram->faults[index];
        uint32_t offset;
        uint32_t word;

        if (!fault->active ||
            fault->address < cw_addr ||
            fault->address >= cw_addr + ODECC_DATA_BYTES)
        {
            continue;
        }

        offset = fault->address - cw_addr;
        memcpy(&word, &data[offset], sizeof(word));

        if (fault->type == DRAM_FAULT_STUCK_AT_0)
        {
            word &= ~fault->bit_mask;
        }
        else if (fault->type == DRAM_FAULT_STUCK_AT_1)
        {
            word |= fault->bit_mask;
        }

        memcpy(&data[offset], &word, sizeof(word));
    }
}

// Sense amplifier에 올라온거 가져온거 = raw 셀 + stuck-at 강제까지 반영된 코드워드를 꺼낸다 (ECC 디코드 전 상태)
static void load_codeword(const DramModel *dram, uint32_t cw_addr,
                          uint8_t data[ODECC_DATA_BYTES])
{
    memcpy(data, &dram->data[cw_addr], ODECC_DATA_BYTES);
    apply_stuck_faults_codeword(dram, cw_addr, data);
}

// Data + ECC parity를 dram->data와 dram->parity에 저장. ECC는 odecc_encode로 계산
static void store_codeword(DramModel *dram, uint32_t cw_addr,
                           const uint8_t data[ODECC_DATA_BYTES])
{
    memcpy(&dram->data[cw_addr], data, ODECC_DATA_BYTES);
    dram->parity[cw_addr / ODECC_DATA_BYTES] = odecc_encode(data);
}

int dram_init(DramModel *dram, size_t size_bytes)
{
    if (dram == NULL || size_bytes == 0)
    {
        return -1;
    }

    memset(dram, 0, sizeof(*dram));

    if (dram_geometry_init(&dram->geometry, size_bytes) != 0)
    {
        return -1;
    }

    dram->data = (uint8_t *)plat_alloc_zero(size_bytes);
    if (dram->data == NULL)
    {
        dram->size_bytes = 0;
        return -1;
    }

    dram->parity = (uint8_t *)plat_alloc_zero(size_bytes / ODECC_DATA_BYTES);
    if (dram->parity == NULL)
    {
        plat_free(dram->data);
        dram->data = NULL;
        return -1;
    }

    dram->size_bytes = size_bytes;
    dram->fault_count = 0;
    dram->odecc_enabled = 1;
    return 0;
}

void dram_free(DramModel *dram)
{
    if (dram == NULL)
    {
        return;
    }

    plat_free(dram->data);
    plat_free(dram->parity);
    dram->data = NULL;
    dram->parity = NULL;
    dram->size_bytes = 0;
    memset(&dram->geometry, 0, sizeof(dram->geometry));
    dram_clear_faults(dram);
    dram_reset_ecc_stats(dram);
}

size_t dram_size_bytes(const DramModel *dram)
{
    if (dram == NULL)
    {
        return 0;
    }

    return dram->size_bytes;
}

size_t dram_modelled_size_bytes(const DramModel *dram)
{
    if (dram == NULL)
    {
        return 0;
    }

    return dram->geometry.modelled_size_bytes;
}

const DramGeometry *dram_geometry(const DramModel *dram)
{
    if (dram == NULL)
    {
        return NULL;
    }

    return &dram->geometry;
}

int dram_is_initialized(const DramModel *dram)
{
    return dram != NULL && dram->data != NULL && dram->size_bytes > 0;
}

int dram_is_valid_range(const DramModel *dram, uint32_t address, size_t length)
{
    if (!dram_is_initialized(dram))
    {
        return 0;
    }

    if ((size_t)address > dram->size_bytes)
    {
        return 0;
    }

    if (length > dram->size_bytes - (size_t)address)
    {
        return 0;
    }

    return 1;
}

/* 쓰기 = 코드워드 단위 read(load_codeword)-modify(odecc_decode)-write(store_codeword) 후 패리티 재계산.
 * RMW 과정의 정정은 통계(DramModule.ecc_corr_count)에 넣지 않는다 (읽기시에만 집계) */
int dram_write32(DramModel *dram, uint32_t address, uint32_t value)
{
    uint8_t codeword[ODECC_DATA_BYTES];
    uint8_t parity;
    uint32_t cw_addr;

    if (!is_aligned32(address))
    {
        return -1;
    }

    if (!dram_is_valid_range(dram, address, sizeof(uint32_t)))
    {
        return -1;
    }

    cw_addr = address & ~(uint32_t)(ODECC_DATA_BYTES - 1U); // & ~15 = & 0b11111111111111111111111111110000 => 16배수
    load_codeword(dram, cw_addr, codeword);
    parity = dram->parity[cw_addr / ODECC_DATA_BYTES];

    if (dram->odecc_enabled)
    {
        OdeccResult ecc;
        // codword 꺼낼때 기존 데이터에 fault있으면 ECC 디코드로 정정
        odecc_decode(codeword, &parity, &ecc);
    }

    memcpy(&codeword[address - cw_addr], &value, sizeof(value));
    store_codeword(dram, cw_addr, codeword);
    return 0;
}

// 읽기 = raw 셀 -> stuck-at 강제 -> ECC 디코드(1비트 정정) -> 워드 반환.
// Error Colleciton : 1비트 정정이면 ecc_corr_count++, 신드롬 미할당(다중 오류 검출)이면 uncorr++ 기록만 되고 -> return 값은 정상 Read
int dram_read32(DramModel *dram, uint32_t address, uint32_t *out_value)
{
    uint8_t codeword[ODECC_DATA_BYTES];
    uint8_t parity;
    uint32_t cw_addr;

    if (out_value == NULL)
    {
        return -1;
    }

    if (!is_aligned32(address))
    {
        return -1;
    }

    if (!dram_is_valid_range(dram, address, sizeof(uint32_t)))
    {
        return -1;
    }

    cw_addr = address & ~(uint32_t)(ODECC_DATA_BYTES - 1U);
    load_codeword(dram, cw_addr, codeword);
    parity = dram->parity[cw_addr / ODECC_DATA_BYTES];

    if (dram->odecc_enabled)
    {
        OdeccResult ecc;
        int rc = odecc_decode(codeword, &parity, &ecc);

        if (rc == ODECC_CORRECTED)
        {
            dram->ecc_corr_count++;
            dram->ecc_last_corr_addr = cw_addr;
        }
        else if (rc == ODECC_UNCORRECTABLE)
        {
            dram->ecc_uncorr_count++;
        }
    }

    memcpy(out_value, &codeword[address - cw_addr], sizeof(*out_value));
    return 0;
}

/* [ COL | BA | BG | ROW ] 비트필드에서 각 필드를 시프트/마스크로 추출 */
int dram_decode_address(const DramModel *dram, uint32_t address, DramAddress *decoded)
{
    const DramGeometry *geometry = NULL;
    uint32_t shift = 0;

    if (dram == NULL || decoded == NULL)
    {
        return -1;
    }

    geometry = &dram->geometry;
    if ((size_t)address >= geometry->modelled_size_bytes)
    {
        return -1;
    }

    memset(decoded, 0, sizeof(*decoded));

    decoded->column = address & ((1U << geometry->col_bits) - 1U);
    shift = geometry->col_bits;

    decoded->bank = (address >> shift) & ((1U << geometry->ba_bits) - 1U);
    shift += geometry->ba_bits;

    decoded->bg = (address >> shift) & ((1U << geometry->bg_bits) - 1U);
    shift += geometry->bg_bits;

    decoded->row = (address >> shift) & ((1U << geometry->row_bits) - 1U);

    return 0;
}

int dram_encode_address(const DramModel *dram, const DramAddress *decoded, uint32_t *address)
{
    const DramGeometry *geometry = NULL;
    uint32_t encoded = 0;
    uint32_t shift = 0;

    if (dram == NULL || decoded == NULL || address == NULL)
    {
        return -1;
    }

    geometry = &dram->geometry;

    if (decoded->bg >= geometry->bank_groups ||
        decoded->bank >= geometry->banks_per_group ||
        decoded->row >= geometry->rows_per_bank ||
        decoded->column >= geometry->row_size_bytes)
    {
        return -1;
    }

    encoded = decoded->column;
    shift = geometry->col_bits;

    encoded |= decoded->bank << shift;
    shift += geometry->ba_bits;

    encoded |= decoded->bg << shift;
    shift += geometry->bg_bits;

    encoded |= decoded->row << shift;

    if ((size_t)encoded >= geometry->modelled_size_bytes)
    {
        return -1;
    }

    *address = encoded;
    return 0;
}

int dram_inject_bit_flip(DramModel *dram, uint32_t address, uint32_t bit_mask)
{
    uint32_t stored_value = 0;

    if (dram == NULL || bit_mask == 0)
    {
        return -1;
    }

    if (!is_aligned32(address))
    {
        return -1;
    }

    if (!dram_is_valid_range(dram, address, sizeof(uint32_t)))
    {
        return -1;
    }

    memcpy(&stored_value, &dram->data[address], sizeof(stored_value));
    stored_value ^= bit_mask;
    memcpy(&dram->data[address], &stored_value, sizeof(stored_value));

    return 0;
}

int dram_add_stuck_fault(DramModel *dram, DramFaultType type,
                         uint32_t address, uint32_t bit_mask)
{
    DramFault *fault = NULL;

    if (dram == NULL || bit_mask == 0)
    {
        return -1;
    }

    if (type != DRAM_FAULT_STUCK_AT_0 && type != DRAM_FAULT_STUCK_AT_1)
    {
        return -1;
    }

    if (!is_aligned32(address))
    {
        return -1;
    }

    if (!dram_is_valid_range(dram, address, sizeof(uint32_t)))
    {
        return -1;
    }

    if (dram->fault_count >= DRAM_MAX_FAULTS)
    {
        return -1;
    }

    fault = &dram->faults[dram->fault_count];
    fault->active = 1;
    fault->type = type;
    fault->address = address;
    fault->bit_mask = bit_mask;
    dram->fault_count++;

    return 0;
}

void dram_clear_faults(DramModel *dram)
{
    if (dram == NULL)
    {
        return;
    }

    memset(dram->faults, 0, sizeof(dram->faults));
    dram->fault_count = 0;
}

size_t dram_fault_count(const DramModel *dram)
{
    if (dram == NULL)
    {
        return 0;
    }

    return dram->fault_count;
}

size_t dram_ecc_correction_count(const DramModel *dram)
{
    return (dram == NULL) ? 0 : dram->ecc_corr_count;
}

size_t dram_ecc_uncorrectable_count(const DramModel *dram)
{
    return (dram == NULL) ? 0 : dram->ecc_uncorr_count;
}

uint32_t dram_ecc_last_corrected_addr(const DramModel *dram)
{
    return (dram == NULL) ? 0 : dram->ecc_last_corr_addr;
}

void dram_reset_ecc_stats(DramModel *dram)
{
    if (dram == NULL)
    {
        return;
    }

    dram->ecc_corr_count = 0;
    dram->ecc_uncorr_count = 0;
    dram->ecc_last_corr_addr = 0;
}

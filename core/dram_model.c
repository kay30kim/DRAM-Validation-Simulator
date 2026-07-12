#include "dram_model.h"
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

static uint32_t apply_stuck_faults32(const DramModel *dram, uint32_t address, uint32_t value)
{
    size_t index;
    uint32_t sensed_value = value;

    if (dram == NULL)
    {
        return value;
    }

    for (index = 0; index < dram->fault_count; index++)
    {
        const DramFault *fault = &dram->faults[index];

        if (!fault->active || fault->address != address)
        {
            continue;
        }

        if (fault->type == DRAM_FAULT_STUCK_AT_0)
        {
            sensed_value &= ~fault->bit_mask;
        }
        else if (fault->type == DRAM_FAULT_STUCK_AT_1)
        {
            sensed_value |= fault->bit_mask;
        }
    }

    return sensed_value;
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

    dram->size_bytes = size_bytes;
    dram->fault_count = 0;
    return 0;
}

void dram_free(DramModel *dram)
{
    if (dram == NULL)
    {
        return;
    }

    plat_free(dram->data);
    dram->data = NULL;
    dram->size_bytes = 0;
    memset(&dram->geometry, 0, sizeof(dram->geometry));
    dram_clear_faults(dram);
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

int dram_write32(DramModel *dram, uint32_t address, uint32_t value)
{
    if (!is_aligned32(address))
    {
        return -1;
    }

    if (!dram_is_valid_range(dram, address, sizeof(uint32_t)))
    {
        return -1;
    }

    memcpy(&dram->data[address], &value, sizeof(value));
    return 0;
}

int dram_read32(const DramModel *dram, uint32_t address, uint32_t *out_value)
{
    uint32_t raw_value = 0;

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

    memcpy(&raw_value, &dram->data[address], sizeof(raw_value));
    *out_value = apply_stuck_faults32(dram, address, raw_value);
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

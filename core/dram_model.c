#include "dram_model.h"
#include "plat.h"

#include <limits.h>
#include <string.h>

static int is_aligned32(uint32_t address)
{
    return (address % 4U) == 0U;
}

static int dram_geometry_init(DramGeometry *geometry, size_t size_bytes)
{
    size_t total_banks = 0;
    size_t bank_size = 0;
    size_t rank_size = 0;
    size_t channel_size = 0;
    size_t modelled_size = 0;
    uint32_t rows_per_bank = 0;

    if (geometry == NULL || size_bytes == 0)
    {
        return -1;
    }

    memset(geometry, 0, sizeof(*geometry));

    total_banks = (size_t)DRAM_DEFAULT_CHANNELS *
                  (size_t)DRAM_DEFAULT_RANKS_PER_CHANNEL *
                  (size_t)DRAM_DEFAULT_BANKS_PER_RANK;
    if (total_banks == 0)
    {
        return -1;
    }

    bank_size = size_bytes / total_banks;
    rows_per_bank = (uint32_t)(bank_size / DRAM_DEFAULT_ROW_SIZE_BYTES);
    if (rows_per_bank == 0)
    {
        return -1;
    }

    bank_size = (size_t)rows_per_bank * (size_t)DRAM_DEFAULT_ROW_SIZE_BYTES;
    rank_size = bank_size * (size_t)DRAM_DEFAULT_BANKS_PER_RANK;
    channel_size = rank_size * (size_t)DRAM_DEFAULT_RANKS_PER_CHANNEL;
    modelled_size = channel_size * (size_t)DRAM_DEFAULT_CHANNELS;

    if (modelled_size == 0 || modelled_size > size_bytes)
    {
        return -1;
    }

    geometry->channels = DRAM_DEFAULT_CHANNELS;
    geometry->ranks_per_channel = DRAM_DEFAULT_RANKS_PER_CHANNEL;
    geometry->banks_per_rank = DRAM_DEFAULT_BANKS_PER_RANK;
    geometry->row_size_bytes = DRAM_DEFAULT_ROW_SIZE_BYTES;
    geometry->rows_per_bank = rows_per_bank;
    geometry->bank_size_bytes = bank_size;
    geometry->rank_size_bytes = rank_size;
    geometry->channel_size_bytes = channel_size;
    geometry->modelled_size_bytes = modelled_size;

    return 0;
}

static uint32_t apply_read_faults32(const DramModel *dram, uint32_t address, uint32_t value)
{
    size_t index;
    uint32_t faulted_value = value;

    if (dram == NULL)
    {
        return value;
    }

    for (index = 0; index < dram->fault_count; index++)
    {
        const DramFault *fault = &dram->faults[index];

        if (fault->active && fault->address == address)
        {
            faulted_value ^= fault->bit_mask;
        }
    }

    return faulted_value;
}

static uint32_t apply_write_faults32(const DramModel *dram, uint32_t address, uint32_t value)
{
    (void)dram;
    (void)address;

    return value;
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
    uint32_t stored_value = 0;

    if (!is_aligned32(address))
    {
        return -1;
    }

    if (!dram_is_valid_range(dram, address, sizeof(uint32_t)))
    {
        return -1;
    }

    stored_value = apply_write_faults32(dram, address, value);
    memcpy(&dram->data[address], &stored_value, sizeof(stored_value));
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
    *out_value = apply_read_faults32(dram, address, raw_value);
    return 0;
}

int dram_decode_address(const DramModel *dram, uint32_t address, DramAddress *decoded)
{
    const DramGeometry *geometry = NULL;
    size_t remaining = 0;

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

    remaining = (size_t)address;

    decoded->channel = (uint32_t)(remaining / geometry->channel_size_bytes);
    remaining %= geometry->channel_size_bytes;

    decoded->rank = (uint32_t)(remaining / geometry->rank_size_bytes);
    remaining %= geometry->rank_size_bytes;

    decoded->bank = (uint32_t)(remaining / geometry->bank_size_bytes);
    remaining %= geometry->bank_size_bytes;

    decoded->row = (uint32_t)(remaining / geometry->row_size_bytes);
    decoded->column = (uint32_t)(remaining % geometry->row_size_bytes);

    return 0;
}

int dram_encode_address(const DramModel *dram, const DramAddress *decoded, uint32_t *address)
{
    const DramGeometry *geometry = NULL;
    size_t encoded = 0;

    if (dram == NULL || decoded == NULL || address == NULL)
    {
        return -1;
    }

    geometry = &dram->geometry;

    if (decoded->channel >= geometry->channels ||
        decoded->rank >= geometry->ranks_per_channel ||
        decoded->bank >= geometry->banks_per_rank ||
        decoded->row >= geometry->rows_per_bank ||
        decoded->column >= geometry->row_size_bytes)
    {
        return -1;
    }

    encoded = ((size_t)decoded->channel * geometry->channel_size_bytes) +
              ((size_t)decoded->rank * geometry->rank_size_bytes) +
              ((size_t)decoded->bank * geometry->bank_size_bytes) +
              ((size_t)decoded->row * geometry->row_size_bytes) +
              (size_t)decoded->column;

    if (encoded > UINT32_MAX || encoded >= geometry->modelled_size_bytes)
    {
        return -1;
    }

    *address = (uint32_t)encoded;
    return 0;
}

int dram_add_bit_flip_fault(DramModel *dram, uint32_t address, uint32_t bit_mask)
{
    DramFault *fault = NULL;

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

    if (dram->fault_count >= DRAM_MAX_FAULTS)
    {
        return -1;
    }

    fault = &dram->faults[dram->fault_count];
    fault->active = 1;
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

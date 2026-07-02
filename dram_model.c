#include "dram_model.h"

#include <stdlib.h>
#include <string.h>

static int is_aligned32(uint32_t address)
{
    return (address % 4U) == 0U;
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

    dram->data = (uint8_t *)calloc(size_bytes, sizeof(uint8_t));
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

    free(dram->data);
    dram->data = NULL;
    dram->size_bytes = 0;
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

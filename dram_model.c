#include "dram_model.h"

#include <stdlib.h>
#include <string.h>

static int is_aligned32(uint32_t address)
{
    return (address % 4U) == 0U;
}

int dram_init(Dram *dram, size_t size_bytes)
{
    if (dram == NULL || size_bytes == 0)
    {
        return -1;
    }

    dram->data = (uint8_t *)calloc(size_bytes, sizeof(uint8_t));
    if (dram->data == NULL)
    {
        dram->size_bytes = 0;
        return -1;
    }

    dram->size_bytes = size_bytes;
    return 0;
}

void dram_free(Dram *dram)
{
    if (dram == NULL)
    {
        return;
    }

    free(dram->data);
    dram->data = NULL;
    dram->size_bytes = 0;
}

size_t dram_size_bytes(const Dram *dram)
{
    if (dram == NULL)
    {
        return 0;
    }

    return dram->size_bytes;
}

int dram_is_initialized(const Dram *dram)
{
    return dram != NULL && dram->data != NULL && dram->size_bytes > 0;
}

int dram_is_valid_range(const Dram *dram, uint32_t address, size_t length)
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

int dram_write32(Dram *dram, uint32_t address, uint32_t value)
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

int dram_read32(const Dram *dram, uint32_t address, uint32_t *out_value)
{
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

    memcpy(out_value, &dram->data[address], sizeof(*out_value));
    return 0;
}
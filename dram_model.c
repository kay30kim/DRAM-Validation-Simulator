#include "dram_model.h"

#include <stdlib.h>

int dram_init(Dram *dram, size_t size_bytes)
{
    if (dram == NULL || size_bytes == 0) {
        return -1;
    }

    dram->data = (uint8_t *)calloc(size_bytes, sizeof(uint8_t));
    if (dram->data == NULL) {
        dram->size_bytes = 0;
        return -1;
    }

    dram->size_bytes = size_bytes;
    return 0;
}

void dram_free(Dram *dram)
{
    if (dram == NULL) {
        return;
    }

    free(dram->data);
    dram->data = NULL;
    dram->size_bytes = 0;
}

size_t dram_size_bytes(const Dram *dram)
{
    if (dram == NULL) {
        return 0;
    }

    return dram->size_bytes;
}

int dram_is_initialized(const Dram *dram)
{
    return dram != NULL && dram->data != NULL && dram->size_bytes > 0;
}

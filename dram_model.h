#ifndef DRAM_MODEL_H
#define DRAM_MODEL_H

#include <stddef.h>
#include <stdint.h>

typedef struct Dram {
    uint8_t *data;
    size_t size_bytes;
} Dram;

int dram_init(Dram *dram, size_t size_bytes);
void dram_free(Dram *dram);
size_t dram_size_bytes(const Dram *dram);
int dram_is_initialized(const Dram *dram);

#endif /* DRAM_MODEL_H */

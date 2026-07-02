#ifndef DRAM_MODEL_H
#define DRAM_MODEL_H

#include <stddef.h>
#include <stdint.h>

#define DRAM_MAX_FAULTS 32U

typedef struct DramFault {
    int active;
    uint32_t address;
    uint32_t bit_mask;
} DramFault;

typedef struct Dram {
    uint8_t *data;
    size_t size_bytes;
    DramFault faults[DRAM_MAX_FAULTS];
    size_t fault_count;
} Dram;

int dram_init(Dram *dram, size_t size_bytes);
void dram_free(Dram *dram);
size_t dram_size_bytes(const Dram *dram);
int dram_is_initialized(const Dram *dram);

int dram_is_valid_range(const Dram *dram, uint32_t address, size_t length);
int dram_write32(Dram *dram, uint32_t address, uint32_t value);
int dram_read32(const Dram *dram, uint32_t address, uint32_t *out_value);

int dram_add_bit_flip_fault(Dram *dram, uint32_t address, uint32_t bit_mask);
void dram_clear_faults(Dram *dram);
size_t dram_fault_count(const Dram *dram);

#endif /* DRAM_MODEL_H */

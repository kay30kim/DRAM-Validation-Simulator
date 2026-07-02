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

typedef struct DramModel {
    uint8_t *data;
    size_t size_bytes;
    DramFault faults[DRAM_MAX_FAULTS];
    size_t fault_count;
} DramModel;

int dram_init(DramModel *dram, size_t size_bytes);
void dram_free(DramModel *dram);
size_t dram_size_bytes(const DramModel *dram);
int dram_is_initialized(const DramModel *dram);

int dram_is_valid_range(const DramModel *dram, uint32_t address, size_t length);
int dram_write32(DramModel *dram, uint32_t address, uint32_t value);
int dram_read32(const DramModel *dram, uint32_t address, uint32_t *out_value);

int dram_add_bit_flip_fault(DramModel *dram, uint32_t address, uint32_t bit_mask);
void dram_clear_faults(DramModel *dram);
size_t dram_fault_count(const DramModel *dram);

#endif /* DRAM_MODEL_H */

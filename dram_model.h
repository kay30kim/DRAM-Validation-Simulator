#ifndef DRAM_MODEL_H
#define DRAM_MODEL_H

#include <stddef.h>
#include <stdint.h>

#define DRAM_MAX_FAULTS 32U

#define DRAM_DEFAULT_CHANNELS 2U
#define DRAM_DEFAULT_RANKS_PER_CHANNEL 1U
#define DRAM_DEFAULT_BANKS_PER_RANK 4U
#define DRAM_DEFAULT_ROW_SIZE_BYTES 8192U

typedef struct DramGeometry
{
    uint32_t channels;
    uint32_t ranks_per_channel;
    uint32_t banks_per_rank;
    uint32_t row_size_bytes;
    uint32_t rows_per_bank;

    size_t bank_size_bytes;
    size_t rank_size_bytes;
    size_t channel_size_bytes;
    size_t modelled_size_bytes;
} DramGeometry;

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

int dram_add_bit_flip_fault(DramModel *dram, uint32_t address, uint32_t bit_mask);
void dram_clear_faults(DramModel *dram);
size_t dram_fault_count(const DramModel *dram);

#endif /* DRAM_MODEL_H */

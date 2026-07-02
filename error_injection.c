#include "error_injection.h"

#include <stdio.h>

int inject_bit_flip32(DramModel *dram,
                      uint32_t address,
                      uint32_t bit_mask,
                      FaultInjectionResult *result)
{
    uint32_t before_value = 0;
    uint32_t after_value = 0;

    if (dram == NULL || bit_mask == 0)
    {
        return -1;
    }

    if (dram_read32(dram, address, &before_value) != 0)
    {
        printf("[INJECT][FAIL] read failed at addr=0x%08X\n", address);
        return -1;
    }

    after_value = before_value ^ bit_mask;

    if (dram_add_bit_flip_fault(dram, address, bit_mask) != 0)
    {
        printf("[INJECT][FAIL] failed to register fault at addr=0x%08X\n", address);
        return -1;
    }

    if (result != NULL)
    {
        result->address = address;
        result->bit_mask = bit_mask;
        result->before_value = before_value;
        result->after_value = after_value;
    }

    printf("[INJECT] registered read bit flip addr=0x%08X mask=0x%08X before=0x%08X expected_read=0x%08X active_faults=%zu\n",
           address,
           bit_mask,
           before_value,
           after_value,
           dram_fault_count(dram));

    return 0;
}

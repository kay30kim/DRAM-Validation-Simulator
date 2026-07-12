#include "error_injection.h"

#include "dlog.h"

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
        dlog_printf("[INJECT][FAIL] read failed at addr=0x%08X\n", address);
        return -1;
    }

    if (dram_inject_bit_flip(dram, address, bit_mask) != 0)
    {
        dlog_printf("[INJECT][FAIL] bit flip failed at addr=0x%08X\n", address);
        return -1;
    }

    if (dram_read32(dram, address, &after_value) != 0)
    {
        dlog_printf("[INJECT][FAIL] read-back failed at addr=0x%08X\n", address);
        return -1;
    }

    if (result != NULL)
    {
        result->address = address;
        result->bit_mask = bit_mask;
        result->before_value = before_value;
        result->after_value = after_value;
    }

    dlog_printf("[INJECT] flipped stored bits addr=0x%08X mask=0x%08X before=0x%08X after=0x%08X\n",
           address,
           bit_mask,
           before_value,
           after_value);

    return 0;
}

int inject_stuck_at32(DramModel *dram,
                      DramFaultType type,
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

    if (type != DRAM_FAULT_STUCK_AT_0 && type != DRAM_FAULT_STUCK_AT_1)
    {
        return -1;
    }

    if (dram_read32(dram, address, &before_value) != 0)
    {
        dlog_printf("[INJECT][FAIL] read failed at addr=0x%08X\n", address);
        return -1;
    }

    if (dram_add_stuck_fault(dram, type, address, bit_mask) != 0)
    {
        dlog_printf("[INJECT][FAIL] failed to register stuck fault at addr=0x%08X\n", address);
        return -1;
    }

    // Error Injection 후 확인용
    if (dram_read32(dram, address, &after_value) != 0)
    {
        dlog_printf("[INJECT][FAIL] read-back failed at addr=0x%08X\n", address);
        return -1;
    }

    if (result != NULL)
    {
        result->address = address;
        result->bit_mask = bit_mask;
        result->before_value = before_value;
        result->after_value = after_value;
    }

    dlog_printf("[INJECT] registered stuck-at-%s addr=0x%08X mask=0x%08X before=0x%08X sensed=0x%08X active_faults=%zu\n",
           (type == DRAM_FAULT_STUCK_AT_0) ? "0" : "1",
           address,
           bit_mask,
           before_value,
           after_value,
           dram_fault_count(dram));

    return 0;
}

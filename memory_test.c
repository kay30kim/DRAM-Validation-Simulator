#include "memory_test.h"

#include <stdio.h>

static int is_aligned32(uint32_t address)
{
    return (address % 4U) == 0U;
}

void memory_test_result_init(MemoryTestResult *result)
{
    if (result == NULL)
    {
        return;
    }

    result->words_tested = 0;
    result->error_count = 0;
    result->first_fail_address = 0;
    result->first_expected = 0;
    result->first_actual = 0;
}

int memory_test_constant_pattern(DramModel *dram,
                                 uint32_t start_address,
                                 size_t length_bytes,
                                 uint32_t pattern,
                                 MemoryTestResult *result)
{
    uint32_t address;
    uint32_t end_address;

    if (result != NULL)
    {
        memory_test_result_init(result);
    }

    if (dram == NULL || length_bytes == 0)
    {
        return -1;
    }

    if (!is_aligned32(start_address) || (length_bytes % sizeof(uint32_t)) != 0U)
    {
        printf("[FAIL] constant pattern test requires 4-byte aligned range\n");
        return -1;
    }

    if (!dram_is_valid_range(dram, start_address, length_bytes))
    {
        printf("[FAIL] constant pattern test range is outside virtual DRAM\n");
        return -1;
    }

    end_address = start_address + (uint32_t)length_bytes;

    printf("[TEST] Constant pattern test: start=0x%08X length=%zu pattern=0x%08X\n",
           start_address, length_bytes, pattern);

    for (address = start_address; address < end_address; address += sizeof(uint32_t))
    {
        if (dram_write32(dram, address, pattern) != 0)
        {
            printf("[FAIL] write failed at addr=0x%08X\n", address);
            return -1;
        }
    }

    for (address = start_address; address < end_address; address += sizeof(uint32_t))
    {
        uint32_t actual = 0;

        if (dram_read32(dram, address, &actual) != 0)
        {
            printf("[FAIL] read failed at addr=0x%08X\n", address);
            return -1;
        }

        if (result != NULL)
        {
            result->words_tested++;
        }

        if (actual != pattern)
        {
            if (result != NULL)
            {
                if (result->error_count == 0)
                {
                    result->first_fail_address = address;
                    result->first_expected = pattern;
                    result->first_actual = actual;
                }

                result->error_count++;
            }
        }
    }

    if (result != NULL && result->error_count > 0)
    {
        printf("[FAIL] constant pattern test completed: words=%zu errors=%zu first_fail=0x%08X expected=0x%08X actual=0x%08X\n",
               result->words_tested,
               result->error_count,
               result->first_fail_address,
               result->first_expected,
               result->first_actual);
        return -1;
    }

    if (result != NULL)
    {
        printf("[PASS] constant pattern test completed: words=%zu errors=%zu\n",
               result->words_tested, result->error_count);
    }
    else
    {
        printf("[PASS] constant pattern test completed\n");
    }

    return 0;
}

int memory_test_verify_constant_pattern(DramModel *dram,
                                        uint32_t start_address,
                                        size_t length_bytes,
                                        uint32_t expected_pattern,
                                        MemoryTestResult *result)
{
    uint32_t address;
    uint32_t end_address;

    if (result != NULL)
    {
        memory_test_result_init(result);
    }

    if (dram == NULL || length_bytes == 0)
    {
        return -1;
    }

    if (!is_aligned32(start_address) || (length_bytes % sizeof(uint32_t)) != 0U)
    {
        printf("[FAIL] constant pattern verify requires 4-byte aligned range\n");
        return -1;
    }

    if (!dram_is_valid_range(dram, start_address, length_bytes))
    {
        printf("[FAIL] constant pattern verify range is outside virtual DRAM\n");
        return -1;
    }

    end_address = start_address + (uint32_t)length_bytes;

    printf("[TEST] Constant pattern verify: start=0x%08X length=%zu expected=0x%08X\n",
           start_address,
           length_bytes,
           expected_pattern);

    for (address = start_address; address < end_address; address += sizeof(uint32_t))
    {
        uint32_t actual = 0;

        if (dram_read32(dram, address, &actual) != 0)
        {
            printf("[FAIL] read failed at addr=0x%08X\n", address);
            return -1;
        }

        if (result != NULL)
        {
            result->words_tested++;
        }

        if (actual != expected_pattern)
        {
            if (result != NULL)
            {
                if (result->error_count == 0)
                {
                    result->first_fail_address = address;
                    result->first_expected = expected_pattern;
                    result->first_actual = actual;
                }

                result->error_count++;
            }
        }
    }

    if (result != NULL && result->error_count > 0)
    {
        printf("[FAIL] constant pattern verify completed: words=%zu errors=%zu first_fail=0x%08X expected=0x%08X actual=0x%08X\n",
               result->words_tested,
               result->error_count,
               result->first_fail_address,
               result->first_expected,
               result->first_actual);
        return -1;
    }

    if (result != NULL)
    {
        printf("[PASS] constant pattern verify completed: words=%zu errors=%zu\n",
               result->words_tested,
               result->error_count);
    }
    else
    {
        printf("[PASS] constant pattern verify completed\n");
    }

    return 0;
}

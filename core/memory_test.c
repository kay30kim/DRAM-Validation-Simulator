#include "memory_test.h"

#include "dlog.h"

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
        dlog_printf("[FAIL] constant pattern test requires 4-byte aligned range\n");
        return -1;
    }

    if (!dram_is_valid_range(dram, start_address, length_bytes))
    {
        dlog_printf("[FAIL] constant pattern test range is outside virtual DRAM\n");
        return -1;
    }

    end_address = start_address + (uint32_t)length_bytes;

    dlog_printf("[TEST] Constant pattern test: start=0x%08X length=%zu pattern=0x%08X\n",
           start_address, length_bytes, pattern);

    for (address = start_address; address < end_address; address += sizeof(uint32_t))
    {
        if (dram_write32(dram, address, pattern) != 0)
        {
            dlog_printf("[FAIL] write failed at addr=0x%08X\n", address);
            return -1;
        }
    }

    for (address = start_address; address < end_address; address += sizeof(uint32_t))
    {
        uint32_t actual = 0;

        if (dram_read32(dram, address, &actual) != 0)
        {
            dlog_printf("[FAIL] read failed at addr=0x%08X\n", address);
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
        dlog_printf("[FAIL] constant pattern test completed: words=%zu errors=%zu first_fail=0x%08X expected=0x%08X actual=0x%08X\n",
               result->words_tested,
               result->error_count,
               result->first_fail_address,
               result->first_expected,
               result->first_actual);
        return -1;
    }

    if (result != NULL)
    {
        dlog_printf("[PASS] constant pattern test completed: words=%zu errors=%zu\n",
               result->words_tested, result->error_count);
    }
    else
    {
        dlog_printf("[PASS] constant pattern test completed\n");
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
        dlog_printf("[FAIL] constant pattern verify requires 4-byte aligned range\n");
        return -1;
    }

    if (!dram_is_valid_range(dram, start_address, length_bytes))
    {
        dlog_printf("[FAIL] constant pattern verify range is outside virtual DRAM\n");
        return -1;
    }

    end_address = start_address + (uint32_t)length_bytes;

    dlog_printf("[TEST] Constant pattern verify: start=0x%08X length=%zu expected=0x%08X\n",
           start_address,
           length_bytes,
           expected_pattern);

    for (address = start_address; address < end_address; address += sizeof(uint32_t))
    {
        uint32_t actual = 0;

        if (dram_read32(dram, address, &actual) != 0)
        {
            dlog_printf("[FAIL] read failed at addr=0x%08X\n", address);
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
        dlog_printf("[FAIL] constant pattern verify completed: words=%zu errors=%zu first_fail=0x%08X expected=0x%08X actual=0x%08X\n",
               result->words_tested,
               result->error_count,
               result->first_fail_address,
               result->first_expected,
               result->first_actual);
        return -1;
    }

    if (result != NULL)
    {
        dlog_printf("[PASS] constant pattern verify completed: words=%zu errors=%zu\n",
               result->words_tested,
               result->error_count);
    }
    else
    {
        dlog_printf("[PASS] constant pattern verify completed\n");
    }

    return 0;
}

/* 위치(BG/BA)마다 다른 패턴을 써서 주소 매핑 오류(aliasing)를 잡는다:
 * 두 위치가 같은 셀로 겹치면 나중에 쓴 패턴이 먼저 쓴 것을 덮어 FAIL */
static uint32_t topology_pattern_for_location(uint32_t base_pattern,
                                              uint32_t bg,
                                              uint32_t bank)
{
    return base_pattern ^ (bg << 28U) ^ (bank << 24U);
}

int memory_test_topology_pattern(DramModel *dram,
                                 uint32_t base_pattern,
                                 MemoryTestResult *result)
{
    const DramGeometry *geometry = dram_geometry(dram);
    uint32_t bg = 0;
    uint32_t bank = 0;
    uint32_t column = 0x200U;

    if (result != NULL)
    {
        memory_test_result_init(result);
    }

    if (dram == NULL || geometry == NULL)
    {
        return -1;
    }

    if (column + sizeof(uint32_t) > geometry->row_size_bytes)
    {
        column = 0;
    }

    dlog_printf("[TEST] Topology-aware pattern test: base_pattern=0x%08X\n", base_pattern);

    for (bg = 0; bg < geometry->bank_groups; bg++)
    {
        for (bank = 0; bank < geometry->banks_per_group; bank++)
        {
            DramAddress dram_address;
            uint32_t linear_address = 0;
            uint32_t pattern = topology_pattern_for_location(base_pattern, bg, bank);

            dram_address.bg = bg;
            dram_address.bank = bank;
            dram_address.row = 0;
            dram_address.column = column;

            if (dram_encode_address(dram, &dram_address, &linear_address) != 0)
            {
                dlog_printf("[FAIL] topology encode failed for BG%u BA%u\n", bg, bank);
                return -1;
            }

            dlog_printf("[TOPO][WRITE] BG%u BA%u addr=0x%08X pattern=0x%08X\n",
                   bg,
                   bank,
                   linear_address,
                   pattern);

            if (dram_write32(dram, linear_address, pattern) != 0)
            {
                dlog_printf("[FAIL] topology write failed at addr=0x%08X\n", linear_address);
                return -1;
            }
        }
    }

    for (bg = 0; bg < geometry->bank_groups; bg++)
    {
        for (bank = 0; bank < geometry->banks_per_group; bank++)
        {
            DramAddress dram_address;
            uint32_t linear_address = 0;
            uint32_t expected = topology_pattern_for_location(base_pattern, bg, bank);
            uint32_t actual = 0;

            dram_address.bg = bg;
            dram_address.bank = bank;
            dram_address.row = 0;
            dram_address.column = column;

            if (dram_encode_address(dram, &dram_address, &linear_address) != 0)
            {
                dlog_printf("[FAIL] topology encode failed for BG%u BA%u\n", bg, bank);
                return -1;
            }

            if (dram_read32(dram, linear_address, &actual) != 0)
            {
                dlog_printf("[FAIL] topology read failed at addr=0x%08X\n", linear_address);
                return -1;
            }

            if (result != NULL)
            {
                result->words_tested++;
            }

            if (actual != expected)
            {
                dlog_printf("[TOPO][FAIL] BG%u BA%u addr=0x%08X expected=0x%08X actual=0x%08X\n",
                       bg,
                       bank,
                       linear_address,
                       expected,
                       actual);

                if (result != NULL)
                {
                    if (result->error_count == 0)
                    {
                        result->first_fail_address = linear_address;
                        result->first_expected = expected;
                        result->first_actual = actual;
                    }

                    result->error_count++;
                }
            }
        }
    }

    if (result != NULL && result->error_count > 0)
    {
        dlog_printf("[FAIL] topology-aware pattern test completed: words=%zu errors=%zu first_fail=0x%08X expected=0x%08X actual=0x%08X\n",
               result->words_tested,
               result->error_count,
               result->first_fail_address,
               result->first_expected,
               result->first_actual);
        return -1;
    }

    if (result != NULL)
    {
        dlog_printf("[PASS] topology-aware pattern test completed: words=%zu errors=%zu\n",
               result->words_tested,
               result->error_count);
    }
    else
    {
        dlog_printf("[PASS] topology-aware pattern test completed\n");
    }

    return 0;
}

#ifndef MEMORY_TEST_H
#define MEMORY_TEST_H

#include "dram_model.h"

#include <stddef.h>
#include <stdint.h>

typedef struct MemoryTestResult {
    size_t words_tested;
    size_t error_count;
    uint32_t first_fail_address;
    uint32_t first_expected;
    uint32_t first_actual;
} MemoryTestResult;

void memory_test_result_init(MemoryTestResult *result);
int memory_test_constant_pattern(DramModel *dram,
                                 uint32_t start_address,
                                 size_t length_bytes,
                                 uint32_t pattern,
                                 MemoryTestResult *result);
int memory_test_verify_constant_pattern(DramModel *dram,
                                        uint32_t start_address,
                                        size_t length_bytes,
                                        uint32_t expected_pattern,
                                        MemoryTestResult *result);
int memory_test_topology_pattern(DramModel *dram,
                                 uint32_t base_pattern,
                                 MemoryTestResult *result);

#endif /* MEMORY_TEST_H */

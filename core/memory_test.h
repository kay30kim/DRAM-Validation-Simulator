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

// March C-: {up(w0); up(r0,w1); up(r1,w0); down(r0,w1); down(r1,w0); up(r0)}
int memory_test_march_c_minus(DramModel *dram,
                              uint32_t start_address,
                              size_t length_bytes,
                              MemoryTestResult *result);

#endif

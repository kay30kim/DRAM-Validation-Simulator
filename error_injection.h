#ifndef ERROR_INJECTION_H
#define ERROR_INJECTION_H

#include "dram_model.h"

#include <stdint.h>

typedef struct FaultInjectionResult {
    uint32_t address;
    uint32_t bit_mask;
    uint32_t before_value;
    uint32_t after_value;
} FaultInjectionResult;

int inject_bit_flip32(DramModel *dram,
                      uint32_t address,
                      uint32_t bit_mask,
                      FaultInjectionResult *result);

#endif /* ERROR_INJECTION_H */

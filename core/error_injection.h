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

// 저장된 셀 값을 1회 반전 (soft error 주입)
int inject_bit_flip32(DramModel *dram,
                      uint32_t address,
                      uint32_t bit_mask,
                      FaultInjectionResult *result);

// 영구 stuck-at 결함 등록 (hard fault 주입)
int inject_stuck_at32(DramModel *dram,
                      DramFaultType type,
                      uint32_t address,
                      uint32_t bit_mask,
                      FaultInjectionResult *result);

#endif

#ifndef LOGGER_H
#define LOGGER_H

#include "dram_model.h"
#include "memory_test.h"

#include <stdint.h>
#include <stdio.h>

typedef struct Logger {
    FILE *stream;
} Logger;

int logger_open(Logger *logger, const char *path);
void logger_close(Logger *logger);

// 시나리오(또는 이벤트 요약) 하나를 CSV 한 행으로 기록.
// res가 NULL이면 카운트 필드는 0, dram이 NULL이 아니면 ECC 통계도 함께 기록
void logger_row(Logger *logger, const char *test_id, const char *result,
                uint32_t start_address, size_t length_bytes, uint32_t pattern,
                const MemoryTestResult *res, const DramModel *dram,
                const char *note);

#endif

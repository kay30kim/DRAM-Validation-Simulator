#ifndef LOGGER_H
#define LOGGER_H

#include "memory_test.h"

#include <stdint.h>
#include <stdio.h>

typedef struct Logger {
    FILE *stream;
} Logger;

int logger_open(Logger *logger, const char *path);
void logger_close(Logger *logger);
int logger_log_smoke_test(Logger *logger,
                          const char *test_name,
                          int pass,
                          uint32_t address,
                          uint32_t expected,
                          uint32_t actual);
int logger_log_memory_test(Logger *logger,
                           const char *test_name,
                           int pass,
                           uint32_t start_address,
                           size_t length_bytes,
                           uint32_t pattern,
                           const MemoryTestResult *result);

#endif /* LOGGER_H */

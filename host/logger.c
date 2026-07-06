#include "logger.h"

#include <stdio.h>

static const char *status_string(int pass)
{
    if (pass)
    {
        return "PASS";
    }

    return "FAIL";
}

int logger_open(Logger *logger, const char *path)
{
    if (logger == NULL || path == NULL)
    {
        return -1;
    }

    logger->stream = fopen(path, "w");
    if (logger->stream == NULL)
    {
        return -1;
    }

    fprintf(logger->stream,
            "test_name,status,start_address,length_bytes,pattern,words_tested,error_count,first_fail_address,first_expected,first_actual\n");
    fflush(logger->stream);
    return 0;
}

void logger_close(Logger *logger)
{
    if (logger == NULL || logger->stream == NULL)
    {
        return;
    }

    fclose(logger->stream);
    logger->stream = NULL;
}

int logger_log_smoke_test(Logger *logger,
                          const char *test_name,
                          int pass,
                          uint32_t address,
                          uint32_t expected,
                          uint32_t actual)
{
    size_t error_count = pass ? 0U : 1U;

    if (logger == NULL || logger->stream == NULL || test_name == NULL)
    {
        return -1;
    }

    fprintf(logger->stream,
            "%s,%s,=\"0x%08X\",%zu,=\"0x%08X\",%zu,%zu,=\"0x%08X\",=\"0x%08X\",=\"0x%08X\"\n",
            test_name,
            status_string(pass),
            address,
            sizeof(uint32_t),
            expected,
            (size_t)1U,
            error_count,
            pass ? 0U : address,
            pass ? 0U : expected,
            pass ? 0U : actual);

    fflush(logger->stream);
    return 0;
}

int logger_log_memory_test(Logger *logger,
                           const char *test_name,
                           int pass,
                           uint32_t start_address,
                           size_t length_bytes,
                           uint32_t pattern,
                           const MemoryTestResult *result)
{
    size_t words_tested = 0;
    size_t error_count = 0;
    uint32_t first_fail_address = 0;
    uint32_t first_expected = 0;
    uint32_t first_actual = 0;

    if (logger == NULL || logger->stream == NULL || test_name == NULL)
    {
        return -1;
    }

    if (result != NULL)
    {
        words_tested = result->words_tested;
        error_count = result->error_count;
        first_fail_address = result->first_fail_address;
        first_expected = result->first_expected;
        first_actual = result->first_actual;
    }

    fprintf(logger->stream,
            "%s,%s,=\"0x%08X\",%zu,=\"0x%08X\",%zu,%zu,=\"0x%08X\",=\"0x%08X\",=\"0x%08X\"\n",
            test_name,
            status_string(pass),
            start_address,
            length_bytes,
            pattern,
            words_tested,
            error_count,
            first_fail_address,
            first_expected,
            first_actual);

    fflush(logger->stream);
    return 0;
}

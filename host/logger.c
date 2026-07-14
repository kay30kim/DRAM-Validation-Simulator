#include "logger.h"

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
            "test_id,result,start_address,length_bytes,pattern,"
            "words_tested,error_count,first_fail_address,first_expected,first_actual,"
            "ecc_corr,ecc_uncorr,note\n");
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

void logger_row(Logger *logger, const char *test_id, const char *result,
                uint32_t start_address, size_t length_bytes, uint32_t pattern,
                const MemoryTestResult *res, const DramModel *dram,
                const char *note)
{
    size_t words_tested = 0;
    size_t error_count = 0;
    uint32_t first_fail_address = 0;
    uint32_t first_expected = 0;
    uint32_t first_actual = 0;
    size_t ecc_corr = 0;
    size_t ecc_uncorr = 0;

    if (logger == NULL || logger->stream == NULL || test_id == NULL)
    {
        return;
    }

    if (res != NULL)
    {
        words_tested = res->words_tested;
        error_count = res->error_count;
        first_fail_address = res->first_fail_address;
        first_expected = res->first_expected;
        first_actual = res->first_actual;
    }

    if (dram != NULL)
    {
        ecc_corr = dram_ecc_correction_count(dram);
        ecc_uncorr = dram_ecc_uncorrectable_count(dram);
    }

    // ="0x..." 래핑은 엑셀이 16진수를 숫자로 오해하는 것을 막기 위함
    fprintf(logger->stream,
            "%s,%s,=\"0x%08X\",%zu,=\"0x%08X\",%zu,%zu,=\"0x%08X\",=\"0x%08X\",=\"0x%08X\",%zu,%zu,%s\n",
            test_id,
            (result != NULL) ? result : "",
            start_address,
            length_bytes,
            pattern,
            words_tested,
            error_count,
            first_fail_address,
            first_expected,
            first_actual,
            ecc_corr,
            ecc_uncorr,
            (note != NULL) ? note : "");

    fflush(logger->stream);
}

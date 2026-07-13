#include "dram_model.h"
#include "error_injection.h"
#include "logger.h"
#include "memory_test.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_DRAM_MB 64U
#define BYTES_PER_MB (1024U * 1024U)
#define TEST_LOG_PATH "dram_test_results.csv"

static int parse_dram_size_mb(int argc, char **argv, size_t *out_mb)
{
    char *endptr = NULL;
    unsigned long value = 0;

    if (out_mb == NULL)
    {
        return -1;
    }

    if (argc < 2)
    {
        *out_mb = DEFAULT_DRAM_MB;
        return 0;
    }

    errno = 0;
    value = strtoul(argv[1], &endptr, 10);
    if (errno != 0 || endptr == argv[1] || *endptr != '\0' || value == 0)
    {
        return -1;
    }

    *out_mb = (size_t)value;
    return 0;
}


static void print_dram_geometry(const DramModel *dram)
{
    const DramGeometry *geometry = dram_geometry(dram);

    if (geometry == NULL)
    {
        return;
    }

    printf("[DRAM] Geometry (datasheet p.4): bank_groups=%u banks/group=%u rows/bank=%u page=%u bytes\n",
           geometry->bank_groups,
           geometry->banks_per_group,
           geometry->rows_per_bank,
           geometry->row_size_bytes);
    printf("[DRAM] Address bits: [ROW(%u) | BG(%u) | BA(%u) | COL(%u)] modelled=%zu bytes (spec ROW max %u)\n",
           geometry->row_bits,
           geometry->bg_bits,
           geometry->ba_bits,
           geometry->col_bits,
           geometry->modelled_size_bytes,
           DRAM_SPEC_ROW_BITS);
}


static void print_decoded_address(uint32_t address, const DramAddress *decoded)
{
    if (decoded == NULL)
    {
        return;
    }

    printf("[ADDR] addr=0x%08X -> BG%u BA%u ROW%u COL%u\n",
           address,
           decoded->bg,
           decoded->bank,
           decoded->row,
           decoded->column);
}

static int dram_address_equals(const DramAddress *left, const DramAddress *right)
{
    if (left == NULL || right == NULL)
    {
        return 0;
    }

    return left->bg == right->bg &&
           left->bank == right->bank &&
           left->row == right->row &&
           left->column == right->column;
}

static int run_address_decode_smoke_test(const DramModel *dram)
{
    const DramGeometry *geometry = dram_geometry(dram);
    uint32_t bg = 0;
    uint32_t bank = 0;
    size_t probes = 0;

    if (geometry == NULL)
    {
        return -1;
    }

    printf("[TEST] Address decode/encode smoke test\n");

    for (bg = 0; bg < geometry->bank_groups; bg++)
    {
        for (bank = 0; bank < geometry->banks_per_group; bank++)
        {
            DramAddress expected;
            DramAddress actual;
            uint32_t encoded_address = 0;

            expected.bg = bg;
            expected.bank = bank;
            expected.row = (geometry->rows_per_bank > 1U) ? 1U : 0U;
            expected.column = 0x40U;

            if (expected.column >= geometry->row_size_bytes)
            {
                expected.column = 0;
            }

            if (dram_encode_address(dram, &expected, &encoded_address) != 0)
            {
                printf("[FAIL] encode failed for BG%u BA%u\n", bg, bank);
                return -1;
            }

            if (dram_decode_address(dram, encoded_address, &actual) != 0)
            {
                printf("[FAIL] decode failed for addr=0x%08X\n", encoded_address);
                return -1;
            }

            print_decoded_address(encoded_address, &actual);

            if (!dram_address_equals(&expected, &actual))
            {
                printf("[FAIL] encode/decode mismatch for addr=0x%08X\n", encoded_address);
                return -1;
            }

            probes++;
        }
    }

    printf("[PASS] address decode/encode smoke test completed: probes=%zu\n", probes);
    return 0;
}

static int run_basic_rw_smoke_test(DramModel *dram,
                                   uint32_t address,
                                   uint32_t expected,
                                   uint32_t *out_actual)
{
    uint32_t actual = 0;

    printf("[TEST] Basic 32-bit read/write smoke test\n");

    if (out_actual != NULL)
    {
        *out_actual = 0;
    }

    if (dram_write32(dram, address, expected) != 0)
    {
        printf("[FAIL] write failed at addr=0x%08X\n", address);
        return -1;
    }

    if (dram_read32(dram, address, &actual) != 0)
    {
        printf("[FAIL] read failed at addr=0x%08X\n", address);
        return -1;
    }

    if (out_actual != NULL)
    {
        *out_actual = actual;
    }

    if (actual != expected)
    {
        printf("[FAIL] addr=0x%08X expected=0x%08X actual=0x%08X\n",
               address, expected, actual);
        return -1;
    }

    printf("[PASS] addr=0x%08X expected=0x%08X actual=0x%08X\n",
           address, expected, actual);
    return 0;
}

int main(int argc, char **argv)
{
    DramModel dram;
    Logger logger;
    MemoryTestResult topology_result;
    MemoryTestResult pattern_result;
    MemoryTestResult verify_result;
    MemoryTestResult stuck_result;
    FaultInjectionResult injection_result;
    FaultInjectionResult stuck_injection;
    size_t dram_mb = 0;
    size_t dram_bytes = 0;
    uint32_t topology_pattern = 0xC3C3C3C3U;
    uint32_t smoke_address = 0x1000U;
    uint32_t smoke_expected = 0xA5A5A5A5U;
    uint32_t smoke_actual = 0;
    uint32_t pattern_start = 0x2000U;
    size_t pattern_length = 64U * 1024U;
    uint32_t pattern = 0xAAAAAAAAU;
    uint32_t injected_address = 0x3000U;
    uint32_t injected_mask = 0x00000001U;
    uint32_t stuck_address = 0x4000U;
    uint32_t stuck_mask = 0x00000002U;
    int topology_pass = 0;
    int smoke_pass = 0;
    int pattern_pass = 0;
    int verify_pass = 0;
    int stuck_pass = 0;
    int bitflip_escaped = 0;
    int stuck_escaped = 0;
    DramAddress corr_location;

    if (parse_dram_size_mb(argc, argv, &dram_mb) != 0)
    {
        printf("[ERROR] Invalid DRAM size argument\n");
        return 1;
    }

    dram_bytes = dram_mb * BYTES_PER_MB;

    printf("[BOOT] C-Based DRAM Validation Simulator\n");
    printf("[DRAM] Initializing virtual DRAM: %zu MB\n", dram_mb);

    if (logger_open(&logger, TEST_LOG_PATH) != 0)
    {
        printf("[ERROR] Failed to open test log: %s\n", TEST_LOG_PATH);
        return 1;
    }

    printf("[LOG] Writing test log to %s\n", TEST_LOG_PATH);

    if (dram_init(&dram, dram_bytes) != 0)
    {
        printf("[ERROR] Failed to initialize virtual DRAM\n");
        logger_close(&logger);
        return 1;
    }

    printf("[DRAM] Virtual DRAM initialized: %zu bytes\n", dram_size_bytes(&dram));
    print_dram_geometry(&dram);

    if (run_address_decode_smoke_test(&dram) != 0)
    {
        dram_free(&dram);
        logger_close(&logger);
        return 1;
    }

    topology_pass = memory_test_topology_pattern(&dram,
                                                  topology_pattern,
                                                  &topology_result) == 0;
    logger_log_memory_test(&logger,
                           "topology_pattern",
                           topology_pass,
                           0U,
                           topology_result.words_tested * sizeof(uint32_t),
                           topology_pattern,
                           &topology_result);

    if (!topology_pass)
    {
        dram_free(&dram);
        logger_close(&logger);
        return 1;
    }

    smoke_pass = run_basic_rw_smoke_test(&dram,
                                         smoke_address,
                                         smoke_expected,
                                         &smoke_actual) == 0;
    logger_log_smoke_test(&logger,
                          "basic_rw_smoke",
                          smoke_pass,
                          smoke_address,
                          smoke_expected,
                          smoke_actual);

    if (!smoke_pass)
    {
        dram_free(&dram);
        logger_close(&logger);
        return 1;
    }

    pattern_pass = memory_test_constant_pattern(&dram,
                                                pattern_start,
                                                pattern_length,
                                                pattern,
                                                &pattern_result) == 0;
    logger_log_memory_test(&logger,
                           "constant_pattern",
                           pattern_pass,
                           pattern_start,
                           pattern_length,
                           pattern,
                           &pattern_result);

    if (!pattern_pass)
    {
        dram_free(&dram);
        logger_close(&logger);
        return 1;
    }

    dram_reset_ecc_stats(&dram);

    if (inject_bit_flip32(&dram,
                          injected_address,
                          injected_mask,
                          &injection_result) != 0)
    {
        dram_free(&dram);
        logger_close(&logger);
        return 1;
    }

    verify_pass = memory_test_verify_constant_pattern(&dram,
                                                      pattern_start,
                                                      pattern_length,
                                                      pattern,
                                                      &verify_result) == 0;
    logger_log_memory_test(&logger,
                           "constant_pattern_after_bit_flip",
                           verify_pass,
                           pattern_start,
                           pattern_length,
                           pattern,
                           &verify_result);

    /* 커밋 5까지는 FAIL 검출이 기대값이었지만, ECC가 생긴 지금은
     * "오염됐는데도 테스트가 통과하는 것"이 기대값이다 (escape) */
    bitflip_escaped = verify_pass &&
                      verify_result.error_count == 0 &&
                      dram_ecc_correction_count(&dram) > 0;
    if (!bitflip_escaped)
    {
        printf("[RESULT] FAIL: expected On-Die ECC escape did not happen\n");
        dram_free(&dram);
        logger_close(&logger);
        return 1;
    }

    dram_decode_address(&dram, dram_ecc_last_corrected_addr(&dram), &corr_location);
    printf("[ECC ] hidden correction at 0x%08X (BG%u BA%u ROW%u COL%u), corrections=%zu\n",
           dram_ecc_last_corrected_addr(&dram),
           corr_location.bg,
           corr_location.bank,
           corr_location.row,
           corr_location.column,
           dram_ecc_correction_count(&dram));
    printf("[RESULT] PASS: single-bit fault escaped the test (masked by On-Die ECC)\n");

    dram_reset_ecc_stats(&dram);

    if (inject_stuck_at32(&dram,
                          DRAM_FAULT_STUCK_AT_0,
                          stuck_address,
                          stuck_mask,
                          &stuck_injection) != 0)
    {
        dram_free(&dram);
        logger_close(&logger);
        return 1;
    }

    /* ECC가 있으면 hard fault(stuck-at)조차 패턴 테스트를 통과해버린다 */
    stuck_pass = memory_test_constant_pattern(&dram,
                                              pattern_start,
                                              pattern_length,
                                              pattern,
                                              &stuck_result) == 0;
    logger_log_memory_test(&logger,
                           "constant_pattern_with_stuck_at_0",
                           stuck_pass,
                           pattern_start,
                           pattern_length,
                           pattern,
                           &stuck_result);

    stuck_escaped = stuck_pass &&
                    stuck_result.error_count == 0 &&
                    dram_ecc_correction_count(&dram) > 0;
    if (!stuck_escaped)
    {
        printf("[RESULT] FAIL: expected On-Die ECC escape did not happen for stuck-at\n");
        dram_free(&dram);
        logger_close(&logger);
        return 1;
    }

    printf("[ECC ] corrections=%zu (stuck-at is re-corrected on every read, not healed)\n",
           dram_ecc_correction_count(&dram));
    printf("[RESULT] PASS: stuck-at fault also escaped the test (hidden hard fault)\n");
    dram_free(&dram);
    logger_close(&logger);
    printf("[DRAM] Virtual DRAM released\n");

    return 0;
}

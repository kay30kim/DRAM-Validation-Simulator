// 호스트 데모: DDR5 스펙 기반 검증 시나리오 모음.
// main()은 목차 역할만 하고, 각 시나리오(tcN)는 자기 안에서
// Arrange(상황 제조) / Act(측정) / Assert(판정)를 끝낸다.
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

#define SMOKE_ADDR 0x1000U
#define SMOKE_PATTERN 0xA5A5A5A5U
#define TOPO_PATTERN 0xC3C3C3C3U
#define REGION_START 0x2000U
#define REGION_LEN (64U * 1024U)
#define REGION_PATTERN 0xAAAAAAAAU
#define BITFLIP_ADDR 0x3000U
#define BITFLIP_MASK 0x00000001U
#define STUCK_ADDR 0x4000U
#define STUCK_MASK 0x00000002U
#define SOFT_ADDR 0x5000U
#define SOFT_MASK 0x00000010U
#define MISCORRECT_ADDR 0x6000U
#define UNCORR_ADDR 0x7000U

#define SCRUB_LOG_MAX 8U

typedef struct ScrubLog
{
    const DramModel *dram;
    const char *name;
    uint32_t addrs[SCRUB_LOG_MAX];
    size_t count;
} ScrubLog;

static void scrub_log_init(ScrubLog *log, const DramModel *dram, const char *name)
{
    size_t i;

    log->dram = dram;
    log->name = name;
    log->count = 0;
    for (i = 0; i < SCRUB_LOG_MAX; i++)
    {
        log->addrs[i] = 0;
    }
}

static void scrub_report(void *ctx, uint32_t cw_addr, uint32_t bit_index,
                         int uncorrectable)
{
    ScrubLog *log = (ScrubLog *)ctx;
    DramAddress location;

    if (log == NULL)
    {
        return;
    }

    if (log->count < SCRUB_LOG_MAX)
    {
        log->addrs[log->count] = cw_addr;
    }
    log->count++;

    if (dram_decode_address(log->dram, cw_addr, &location) != 0)
    {
        return;
    }

    printf("[SCRUB] %s: %s addr=0x%08X (BG%u BA%u ROW%u COL%u) bit=%u\n",
           log->name,
           uncorrectable ? "UNCORRECTABLE" : "corrected",
           cw_addr,
           location.bg,
           location.bank,
           location.row,
           location.column,
           bit_index);
}

static void log_scrub_summary(Logger *logger, const char *name,
                              const ScrubLog *log, size_t events)
{
    MemoryTestResult summary;

    memory_test_result_init(&summary);
    summary.words_tested = REGION_LEN / ODECC_DATA_BYTES;
    summary.error_count = events;
    summary.first_fail_address = (log->count > 0) ? log->addrs[0] : 0;
    logger_log_memory_test(logger, name, 1, REGION_START, REGION_LEN,
                           REGION_PATTERN, &summary);
}

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

// TC1. 주소 인코드/디코드 라운드트립 (datasheet p.4 Address Table)
static int tc1_address_decode(const DramModel *dram)
{
    const DramGeometry *geometry = dram_geometry(dram);
    uint32_t bg;
    uint32_t bank;
    size_t probes = 0;

    if (geometry == NULL)
    {
        return -1;
    }

    printf("[TEST] Address decode/encode smoke test\n");

    // Act + Assert: 모든 BG/BA 조합을 왕복시켜 원본과 일치하는지 확인
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

// TC2. 위치(BG/BA)별 고유 패턴으로 주소 매핑 aliasing 검사
static int tc2_topology_pattern(DramModel *dram, Logger *logger)
{
    MemoryTestResult result;
    int pass;

    // Act
    pass = memory_test_topology_pattern(dram, TOPO_PATTERN, &result) == 0;

    logger_log_memory_test(logger, "topology_pattern", pass, 0U,
                           result.words_tested * sizeof(uint32_t),
                           TOPO_PATTERN, &result);

    // Assert
    return pass ? 0 : -1;
}

// TC3. 32비트 read/write 기본 동작
static int tc3_basic_rw(DramModel *dram, Logger *logger)
{
    uint32_t actual = 0;
    int pass = 0;

    printf("[TEST] Basic 32-bit read/write smoke test\n");

    // Act
    if (dram_write32(dram, SMOKE_ADDR, SMOKE_PATTERN) != 0)
    {
        printf("[FAIL] write failed at addr=0x%08X\n", SMOKE_ADDR);
    }
    else if (dram_read32(dram, SMOKE_ADDR, &actual) != 0)
    {
        printf("[FAIL] read failed at addr=0x%08X\n", SMOKE_ADDR);
    }
    else if (actual != SMOKE_PATTERN)
    {
        printf("[FAIL] addr=0x%08X expected=0x%08X actual=0x%08X\n",
               SMOKE_ADDR, SMOKE_PATTERN, actual);
    }
    else
    {
        printf("[PASS] addr=0x%08X expected=0x%08X actual=0x%08X\n",
               SMOKE_ADDR, SMOKE_PATTERN, actual);
        pass = 1;
    }

    logger_log_smoke_test(logger, "basic_rw_smoke", pass, SMOKE_ADDR,
                          SMOKE_PATTERN, actual);

    // Assert
    return pass ? 0 : -1;
}

// TC4. 결함 없는 베이스라인: 채운 패턴이 그대로 읽혀야 한다
static int tc4_constant_pattern(DramModel *dram, Logger *logger)
{
    MemoryTestResult result;
    int pass;

    // Act
    pass = memory_test_constant_pattern(dram, REGION_START, REGION_LEN,
                                        REGION_PATTERN, &result) == 0;

    logger_log_memory_test(logger, "constant_pattern", pass, REGION_START,
                           REGION_LEN, REGION_PATTERN, &result);

    // Assert
    return pass ? 0 : -1;
}

// TC5. On-Die ECC escape: 1비트 결함이 정정돼 테스트가 "통과"해버린다
static int tc5_odecc_escape(DramModel *dram, Logger *logger)
{
    FaultInjectionResult injection;
    MemoryTestResult result;
    DramAddress location;
    int verify_pass;
    int escaped;

    // Arrange: 정정 통계를 0으로 만들고 soft error 1비트 주입
    dram_reset_ecc_stats(dram);
    if (inject_bit_flip32(dram, BITFLIP_ADDR, BITFLIP_MASK, &injection) != 0)
    {
        return -1;
    }

    // Act: 오염된 영역을 패턴 verify
    verify_pass = memory_test_verify_constant_pattern(dram, REGION_START,
                                                      REGION_LEN,
                                                      REGION_PATTERN,
                                                      &result) == 0;
    logger_log_memory_test(logger, "constant_pattern_after_bit_flip",
                           verify_pass, REGION_START, REGION_LEN,
                           REGION_PATTERN, &result);

    // Assert: PASS인데 정정 흔적이 남아 있어야 escape 재현 성공
    escaped = verify_pass &&
              result.error_count == 0 &&
              dram_ecc_correction_count(dram) > 0;
    if (!escaped)
    {
        printf("[RESULT] FAIL: expected On-Die ECC escape did not happen\n");
        return -1;
    }

    dram_decode_address(dram, dram_ecc_last_corrected_addr(dram), &location);
    printf("[ECC ] hidden correction at 0x%08X (BG%u BA%u ROW%u COL%u), corrections=%zu\n",
           dram_ecc_last_corrected_addr(dram),
           location.bg,
           location.bank,
           location.row,
           location.column,
           dram_ecc_correction_count(dram));
    printf("[RESULT] PASS: single-bit fault escaped the test (masked by On-Die ECC)\n");
    return 0;
}

// TC6. stuck-at escape: hard fault조차 패턴 테스트를 통과해버린다
static int tc6_stuck_escape(DramModel *dram, Logger *logger)
{
    FaultInjectionResult injection;
    MemoryTestResult result;
    int pass;
    int escaped;

    // Arrange
    dram_reset_ecc_stats(dram);
    if (inject_stuck_at32(dram, DRAM_FAULT_STUCK_AT_0, STUCK_ADDR, STUCK_MASK,
                          &injection) != 0)
    {
        return -1;
    }

    // Act: 전체 재기록+verify. TC5의 soft는 이때 치유되고 stuck만 남는다
    pass = memory_test_constant_pattern(dram, REGION_START, REGION_LEN,
                                        REGION_PATTERN, &result) == 0;
    logger_log_memory_test(logger, "constant_pattern_with_stuck_at_0", pass,
                           REGION_START, REGION_LEN, REGION_PATTERN, &result);

    // Assert
    escaped = pass &&
              result.error_count == 0 &&
              dram_ecc_correction_count(dram) > 0;
    if (!escaped)
    {
        printf("[RESULT] FAIL: expected On-Die ECC escape did not happen for stuck-at\n");
        return -1;
    }

    printf("[ECC ] corrections=%zu (stuck-at is re-corrected on every read, not healed)\n",
           dram_ecc_correction_count(dram));
    printf("[RESULT] PASS: stuck-at fault also escaped the test (hidden hard fault)\n");
    return 0;
}

// TC7. scrub 스크린: 반복 scrub으로 soft(치유됨)와 hard(재발)를 분류
static int tc7_scrub_screen(DramModel *dram, Logger *logger)
{
    FaultInjectionResult injection;
    ScrubLog pass1_log;
    ScrubLog pass2_log;
    size_t pass1_events;
    size_t pass2_events;
    uint32_t stuck_cw = STUCK_ADDR & ~(uint32_t)(ODECC_DATA_BYTES - 1U);
    uint32_t soft_cw = SOFT_ADDR & ~(uint32_t)(ODECC_DATA_BYTES - 1U);
    int screen_ok;

    // Arrange: hard(stuck)는 TC6에서 생존 중. soft를 추가해 두 종류를 공존시킨다
    if (inject_bit_flip32(dram, SOFT_ADDR, SOFT_MASK, &injection) != 0)
    {
        return -1;
    }

    // Act 1: scrub 1회차 - 오류를 찾아 정정값을 재기록(치유 시도)
    printf("[TEST] Scrub pass #1: scan every codeword, correct and write back\n");
    scrub_log_init(&pass1_log, dram, "pass1");
    pass1_events = dram_scrub_range(dram, REGION_START, REGION_LEN,
                                    scrub_report, &pass1_log);
    printf("[SCRUB] pass #1 done: events=%zu\n", pass1_events);
    log_scrub_summary(logger, "scrub_pass1", &pass1_log, pass1_events);

    // Act 2: scrub 2회차 - 재기록으로 안 나은 놈(hard)만 다시 나타난다
    printf("[TEST] Scrub pass #2: repeat - only hard faults should reappear\n");
    scrub_log_init(&pass2_log, dram, "pass2");
    pass2_events = dram_scrub_range(dram, REGION_START, REGION_LEN,
                                    scrub_report, &pass2_log);
    printf("[SCRUB] pass #2 done: events=%zu\n", pass2_events);
    log_scrub_summary(logger, "scrub_pass2", &pass2_log, pass2_events);

    // Assert: 1회차 2건(soft+hard), 2회차엔 stuck 자리 1건만 재발해야 한다
    screen_ok = (pass1_events == 2) &&
                (pass2_events == 1) &&
                (pass2_log.count == 1) &&
                (pass2_log.addrs[0] == stuck_cw);
    if (!screen_ok)
    {
        printf("[RESULT] FAIL: scrub screen did not classify faults as expected\n");
        return -1;
    }

    printf("[SCREEN] 0x%08X: corrected once, gone after rewrite -> SOFT error (healed)\n",
           soft_cw);
    printf("[SCREEN] 0x%08X: reappeared after rewrite -> HARD fault candidate (screen out)\n",
           stuck_cw);
    printf("[RESULT] PASS: repeated scrub separated soft error from hard fault\n");
    return 0;
}

// TC8. 2비트 결함: SEC의 한계. 신드롬이 남의 이름표와 겹치면 오정정,
// 미할당 값이면 정정 불가로 검출된다
static int tc8_double_bit(DramModel *dram, Logger *logger)
{
    MemoryTestResult result;
    int verify_fail;
    int miscorrected;
    int uncorrectable;

    // Arrange: 이전 시나리오의 stuck을 걷어내고 영역을 깨끗하게 재구축
    dram_clear_faults(dram);
    if (memory_test_constant_pattern(dram, REGION_START, REGION_LEN,
                                     REGION_PATTERN, &result) != 0)
    {
        return -1;
    }

    // Act 1: 한 코드워드에 2비트 주입. 자리0(이름표3)+자리1(이름표5)
    //        -> 신드롬 6 = 자리2의 이름표 -> 엉뚱한 3번째 비트를 "정정"
    printf("[TEST] Double-bit fault #1: bits 0,1 in one codeword (syndrome hits another position)\n");
    if (inject_bit_flip32(dram, MISCORRECT_ADDR, 0x1U, NULL) != 0 ||
        inject_bit_flip32(dram, MISCORRECT_ADDR, 0x2U, NULL) != 0)
    {
        return -1;
    }

    // 주입 과정의 read-back 정정은 측정에서 제외
    dram_reset_ecc_stats(dram);
    verify_fail = memory_test_verify_constant_pattern(dram, MISCORRECT_ADDR,
                                                      ODECC_DATA_BYTES,
                                                      REGION_PATTERN,
                                                      &result) != 0;
    logger_log_memory_test(logger, "double_bit_miscorrection",
                           verify_fail ? 0 : 1, MISCORRECT_ADDR,
                           ODECC_DATA_BYTES, REGION_PATTERN, &result);

    miscorrected = verify_fail &&
                   result.error_count == 1 &&
                   result.first_actual == (REGION_PATTERN ^ 0x7U) &&
                   dram_ecc_correction_count(dram) > 0;
    if (!miscorrected)
    {
        printf("[RESULT] FAIL: expected miscorrection did not happen\n");
        return -1;
    }

    printf("[SEC ] expected=0x%08X actual=0x%08X -> a 3rd bit was flipped by \"correction\"\n",
           REGION_PATTERN, result.first_actual);
    printf("[SEC ] corr_count=%zu <- the counter believes it fixed something (it lied)\n",
           dram_ecc_correction_count(dram));

    // Act 2: 자리0(이름표3)+자리127(이름표136) -> 신드롬 139 = 미할당
    //        -> 정정 불가로 검출되고 데이터는 손대지 않는다
    printf("[TEST] Double-bit fault #2: bits 0,127 (syndrome unassigned)\n");
    if (inject_bit_flip32(dram, UNCORR_ADDR, 0x1U, NULL) != 0 ||
        inject_bit_flip32(dram, UNCORR_ADDR + 12U, 0x80000000U, NULL) != 0)
    {
        return -1;
    }

    // 주입 과정의 read-back 정정은 측정에서 제외
    dram_reset_ecc_stats(dram);
    verify_fail = memory_test_verify_constant_pattern(dram, UNCORR_ADDR,
                                                      ODECC_DATA_BYTES,
                                                      REGION_PATTERN,
                                                      &result) != 0;
    logger_log_memory_test(logger, "double_bit_uncorrectable",
                           verify_fail ? 0 : 1, UNCORR_ADDR,
                           ODECC_DATA_BYTES, REGION_PATTERN, &result);

    uncorrectable = verify_fail &&
                    result.error_count == 2 &&
                    dram_ecc_uncorrectable_count(dram) > 0 &&
                    dram_ecc_correction_count(dram) == 0;
    if (!uncorrectable)
    {
        printf("[RESULT] FAIL: expected uncorrectable detection did not happen\n");
        return -1;
    }

    printf("[SEC ] uncorrectable detected: uncorr_count=%zu, data left untouched (2 wrong words)\n",
           dram_ecc_uncorrectable_count(dram));
    printf("[RESULT] PASS: SEC limit demonstrated - double-bit faults miscorrect or flag uncorrectable\n");
    return 0;
}

int main(int argc, char **argv)
{
    DramModel dram;
    Logger logger;
    size_t dram_mb = 0;
    size_t dram_bytes = 0;

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

    // 시나리오 목차: 하나라도 실패하면 즉시 중단
    if (tc1_address_decode(&dram) != 0 ||
        tc2_topology_pattern(&dram, &logger) != 0 ||
        tc3_basic_rw(&dram, &logger) != 0 ||
        tc4_constant_pattern(&dram, &logger) != 0 ||
        tc5_odecc_escape(&dram, &logger) != 0 ||
        tc6_stuck_escape(&dram, &logger) != 0 ||
        tc7_scrub_screen(&dram, &logger) != 0 ||
        tc8_double_bit(&dram, &logger) != 0)
    {
        dram_free(&dram);
        logger_close(&logger);
        return 1;
    }

    dram_free(&dram);
    logger_close(&logger);
    printf("[DRAM] Virtual DRAM released\n");

    return 0;
}

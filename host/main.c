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
#include <string.h>

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
#define MATRIX_ADDR (REGION_START + 0x100U)
#define MATRIX_VICTIM (MATRIX_ADDR + 4U)

#define RETENTION_ADDR 0x14000U
#define RETENTION_LEN 64U
#define RETENTION_PATTERN 0xFFFFFFFFU
#define RETENTION_MASK 0x00000001U
#define RETENTION_HOLD_NS 40000000ULL // 40ms은(retention없이 버틸수있는 기간) 그냥 임의로 한 값이고 tREF인 32ms 주기엔 괜찮고 고온 절반(20ms)엔 죽게 // 단위 ns!

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
    logger_row(logger, name, "PASS", REGION_START, REGION_LEN, REGION_PATTERN,
               &summary, log->dram, "scrub_events_in_error_count");
}

typedef struct CliOptions
{
    size_t size_mb;
    const char *test_name;
    const char *inject_spec;
} CliOptions;

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("  --size-mb <n>              virtual DRAM size in MB (default %u)\n", DEFAULT_DRAM_MB);
    printf("  --test <name>              all(default), tc1..tc11, constant, march\n");
    printf("  --inject <type:addr:mask>  type: bitflip, sa0, sa1  ex) sa1:0x3000:0x1\n");
}

static int parse_u32(const char *text, uint32_t *out)
{
    char *end = NULL;
    unsigned long value = 0;

    errno = 0;
    value = strtoul(text, &end, 0); // 밑수 0: "0x" 접두어도 알아서 처리
    if (errno != 0 || end == text || *end != '\0' || value > 0xFFFFFFFFUL)
    {
        return -1;
    }

    *out = (uint32_t)value;
    return 0;
}

static int parse_args(int argc, char **argv, CliOptions *opt)
{
    int i;

    opt->size_mb = DEFAULT_DRAM_MB;
    opt->test_name = "all";
    opt->inject_spec = NULL;

    for (i = 1; i < argc; i++)
    {
        const char *name = argv[i];
        const char *value = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (strcmp(name, "--help") == 0)
        {
            return 1;
        }

        if (value == NULL)
        {
            return -1;
        }

        if (strcmp(name, "--size-mb") == 0)
        {
            uint32_t mb = 0;

            if (parse_u32(value, &mb) != 0 || mb == 0)
            {
                return -1;
            }
            opt->size_mb = mb;
        }
        else if (strcmp(name, "--test") == 0)
        {
            opt->test_name = value;
        }
        else if (strcmp(name, "--inject") == 0)
        {
            opt->inject_spec = value;
        }
        else
        {
            return -1;
        }
        i += 1;
    }

    return 0;
}

// "bitflip:0x3000:0x1" 꼴을 쪼개서 주입한다.
// tc 시나리오들은 결함을 스스로 만드니까, 이 옵션은 --test constant/march와 짝
static int apply_inject(DramModel *dram, const char *spec)
{
    char type[16];
    const char *first = NULL;
    const char *second = NULL;
    char *end = NULL;
    size_t type_len = 0;
    uint32_t address = 0;
    uint32_t mask = 0;
    int rc = -1;

    first = strchr(spec, ':');
    if (first == NULL)
    {
        return -1;
    }

    second = strchr(first + 1, ':');
    if (second == NULL)
    {
        return -1;
    }

    type_len = (size_t)(first - spec);
    if (type_len == 0 || type_len >= sizeof(type))
    {
        return -1;
    }
    memcpy(type, spec, type_len);
    type[type_len] = '\0';

    errno = 0;
    address = (uint32_t)strtoul(first + 1, &end, 0);
    if (errno != 0 || end != second)
    {
        return -1;
    }

    if (parse_u32(second + 1, &mask) != 0)
    {
        return -1;
    }

    if (strcmp(type, "bitflip") == 0)
    {
        rc = dram_inject_bit_flip(dram, address, mask);
    }
    else if (strcmp(type, "sa0") == 0)
    {
        rc = dram_add_stuck_fault(dram, DRAM_FAULT_STUCK_AT_0, address, mask);
    }
    else if (strcmp(type, "sa1") == 0)
    {
        rc = dram_add_stuck_fault(dram, DRAM_FAULT_STUCK_AT_1, address, mask);
    }

    if (rc == 0)
    {
        printf("[CLI ] injected %s addr=0x%08X mask=0x%08X\n", type, address, mask);
    }

    return rc;
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
static int tc1_address_decode(DramModel *dram, Logger *logger)
{
    const DramGeometry *geometry = dram_geometry(dram);
    uint32_t bg;
    uint32_t bank;
    size_t probes = 0;

    (void)logger; // tc1은 CSV에 남길 게 없다

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

    logger_row(logger, "topology_pattern", pass ? "PASS" : "FAIL", 0U,
               result.words_tested * sizeof(uint32_t), TOPO_PATTERN,
               &result, dram, "bg_ba_aliasing_check");

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

    logger_row(logger, "basic_rw_smoke", pass ? "PASS" : "FAIL", SMOKE_ADDR,
               sizeof(uint32_t), SMOKE_PATTERN, NULL, dram, "single_word_rw");

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

    logger_row(logger, "constant_pattern", pass ? "PASS" : "FAIL", REGION_START,
               REGION_LEN, REGION_PATTERN, &result, dram, "clean_baseline");

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
    logger_row(logger, "constant_pattern_after_bit_flip",
               verify_pass ? "PASS" : "FAIL", REGION_START, REGION_LEN,
               REGION_PATTERN, &result, dram, "single_bit_masked_by_odecc");

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
    logger_row(logger, "constant_pattern_with_stuck_at_0",
               pass ? "PASS" : "FAIL", REGION_START, REGION_LEN,
               REGION_PATTERN, &result, dram, "hard_fault_masked_by_odecc");

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
    logger_row(logger, "double_bit_miscorrection", verify_fail ? "FAIL" : "PASS",
               MISCORRECT_ADDR, ODECC_DATA_BYTES, REGION_PATTERN, &result,
               dram, "expected_fail_sec_miscorrected_3rd_bit");

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
    logger_row(logger, "double_bit_uncorrectable", verify_fail ? "FAIL" : "PASS",
               UNCORR_ADDR, ODECC_DATA_BYTES, REGION_PATTERN, &result,
               dram, "expected_fail_sec_uncorrectable_detected");

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

// TC9. March C-: SAF/TF/coupling을 결정적으로 잡는 표준 알고리즘.
// 깨끗한 메모리에서 6요소 전체가 초록이어야 한다
static int tc9_march_c_minus(DramModel *dram, Logger *logger)
{
    MemoryTestResult result;
    int pass;

    // Arrange: 첫 요소 M0(w0)가 전 영역을 덮어쓰므로 이전 잔재는 자동 정리.
    //          측정 카운터만 초기화
    dram_reset_ecc_stats(dram);

    // Act
    pass = memory_test_march_c_minus(dram, REGION_START, REGION_LEN,
                                     &result) == 0;
    logger_row(logger, "march_c_minus", pass ? "PASS" : "FAIL", REGION_START,
               REGION_LEN, 0U, &result, dram, "van_de_goor_march_c_minus");

    // Assert
    return pass ? 0 : -1;
}

// TC10. 커버리지 매트릭스: 결함 모델 x 알고리즘 검출표
static const char *kMatrixFaults[5] =
{
    "stuck_at_0", "stuck_at_1", "transition_up", "transition_down",
    "coupling_inv"
};

static const char *kMatrixTests[2] = { "constant", "march" };

static int matrix_inject(DramModel *dram, size_t fault_index)
{
    switch (fault_index)
    {
    case 0:
        return dram_add_stuck_fault(dram, DRAM_FAULT_STUCK_AT_0,
                                    MATRIX_ADDR, 0x1U);
    case 1:
        return dram_add_stuck_fault(dram, DRAM_FAULT_STUCK_AT_1,
                                    MATRIX_ADDR, 0x1U);
    case 2:
        return dram_add_transition_fault(dram, DRAM_FAULT_TRANSITION_UP,
                                         MATRIX_ADDR, 0x2U);
    case 3:
        return dram_add_transition_fault(dram, DRAM_FAULT_TRANSITION_DOWN,
                                         MATRIX_ADDR, 0x2U);
    case 4:
        return dram_add_coupling_fault(dram, MATRIX_ADDR, 0x2U,
                                       MATRIX_VICTIM, 0x1U);
    default:
        return -1;
    }
}

// 검출 여부 반환 (1 = detect / 0 = miss)
static int matrix_run_test(DramModel *dram, size_t test_index,
                           MemoryTestResult *result)
{
    if (test_index == 0)
    {
        return memory_test_constant_pattern(dram, REGION_START, REGION_LEN,
                                            REGION_PATTERN, result) != 0;
    }
    return memory_test_march_c_minus(dram, REGION_START, REGION_LEN,
                                     result) != 0;
}

static void matrix_reset_region(DramModel *dram)
{
    uint32_t address;

    for (address = REGION_START; address < REGION_START + REGION_LEN;
         address += (uint32_t)sizeof(uint32_t))
    {
        (void)dram_write32(dram, address, 0U);
    }
}

static int tc10_coverage_matrix(DramModel *dram, Logger *logger)
{
    // 이론 기대값: constant는 패턴과 겹치는 결함을 놓치고 March C-는 전부 잡는다
    static const int kExpected[5][2] =
    {
        { 0, 1 }, // stuck_at_0     : 패턴 bit0=0이라 constant는 MISS
        { 1, 1 }, // stuck_at_1
        { 1, 1 }, // transition_up
        { 0, 1 }, // transition_down: constant는 1->0 전이를 안 시켜서 MISS
        { 0, 1 }, // coupling_inv   : victim이 나중에 덮여서 constant는 MISS
    };
    int actual[5][2];
    size_t fault;
    size_t test;
    int match = 1;

    printf("[TEST] Coverage matrix: 5 fault models x 2 algorithms\n");
    printf("[NOTE] ODECC disabled for this scenario - raw algorithm coverage\n");
    dram_set_odecc_enabled(dram, 0);

    for (fault = 0; fault < 5U; fault++)
    {
        for (test = 0; test < 2U; test++)
        {
            MemoryTestResult result;
            char row_id[64];

            // Arrange: 결함 초기화 -> 저장소 0 리셋 -> 이번 결함 주입
            dram_clear_faults(dram);
            matrix_reset_region(dram);
            if (matrix_inject(dram, fault) != 0)
            {
                dram_set_odecc_enabled(dram, 1);
                return -1;
            }

            // Act
            actual[fault][test] = matrix_run_test(dram, test, &result);

            snprintf(row_id, sizeof(row_id), "matrix_%s_%s",
                     kMatrixFaults[fault], kMatrixTests[test]);
            logger_row(logger, row_id,
                       actual[fault][test] ? "DETECT" : "MISS",
                       MATRIX_ADDR, REGION_LEN, REGION_PATTERN, &result,
                       dram, "coverage_matrix_odecc_off");

            if (actual[fault][test] != kExpected[fault][test])
            {
                match = 0;
            }
        }
    }

    dram_clear_faults(dram);
    dram_set_odecc_enabled(dram, 1);

    printf("[MATRIX] %-16s %-9s %s\n", "fault", "constant", "march_c-");
    for (fault = 0; fault < 5U; fault++)
    {
        printf("[MATRIX] %-16s %-9s %s\n",
               kMatrixFaults[fault],
               actual[fault][0] ? "DETECT" : "MISS",
               actual[fault][1] ? "DETECT" : "MISS");
    }

    if (!match)
    {
        printf("[RESULT] FAIL: coverage matrix does not match theory\n");
        return -1;
    }

    printf("[RESULT] PASS: coverage matrix matches theory (march c- covers all modeled faults)\n");
    return 0;
}

static void retention_fill(DramModel *dram)
{
    uint32_t address;

    for (address = RETENTION_ADDR; address < RETENTION_ADDR + RETENTION_LEN;
         address += (uint32_t)sizeof(uint32_t))
    {
        (void)dram_write32(dram, address, RETENTION_PATTERN);
    }
}

// 조건 하나(온도, 대기 시간, refresh 여부)를 돌리고 유지=1 / 방전=0을 돌려준다
static int retention_case(DramModel *dram, Logger *logger, const char *id,
                          int temp_c, uint64_t step_ns, int steps,
                          int do_refresh, const char *note)
{
    MemoryTestResult result;
    int held;
    int step;

    dram_set_temperature(dram, temp_c);
    dram_refresh(dram);
    retention_fill(dram);

    for (step = 0; step < steps; step++)
    {
        dram_advance_time(dram, step_ns);
        if (do_refresh)
        {
            dram_refresh(dram);
        }
    }

    held = memory_test_verify_constant_pattern(dram, RETENTION_ADDR,
                                               RETENTION_LEN, RETENTION_PATTERN,
                                               &result) == 0;
    logger_row(logger, id, held ? "PASS" : "FAIL", RETENTION_ADDR, RETENTION_LEN,
               RETENTION_PATTERN, &result, dram, note);
    printf("[RET ] %s @%dC: %s\n", id, temp_c, held ? "held" : "data lost");
    return held;
}

// TC11. retention/refresh: refresh를 제때 못 받으면 약한 셀이 방전(1->0)된다.
// 근거: datasheet p.23 - tREFI(3.9us) x 8192회 = 32ms(tREF) 주기,
// Table 2: 85도 초과 시 tREFI/2. ODECC는 꺼서 raw 셀 거동만 본다
static int tc11_retention_refresh(DramModel *dram, Logger *logger)
{
    int ok = 1;

    printf("[TEST] Retention/refresh: weak cell decays without timely refresh\n");
    printf("[NOTE] ODECC(On Die Error Correction Code) disabled - raw retention only\n");
    dram_set_odecc_enabled(dram, 0);
    dram_clear_faults(dram);

    // 약한 셀 하나: 40ms 버팀. tREFW(32ms) 주기 refresh면 살고 놓치면 죽는다
    if (dram_add_retention_fault(dram, RETENTION_ADDR, RETENTION_MASK,
                                 RETENTION_HOLD_NS) != 0)
    {
        dram_set_odecc_enabled(dram, 1);
        return -1;
    }

    // 25도: refresh 없이 50ms(>40ms)면 방전, tREFW마다 refresh면 유지
    ok &= !retention_case(dram, logger, "ret_25c_no_refresh_50ms", 25,
                          50000000ULL, 1, 0, "no_refresh_decays");
    ok &= retention_case(dram, logger, "ret_25c_refresh_32ms", 25,
                         DRAM_TREFW_NS, 3, 1, "trefw_refresh_holds");

    // 95도: 셀이 20ms밖에 못 버틴다. 32ms 주기 refresh는 늦고 16ms는 안 늦는다
    ok &= !retention_case(dram, logger, "ret_95c_refresh_32ms", 95,
                          DRAM_TREFW_NS, 1, 1, "hot_normal_refresh_late");
    ok &= retention_case(dram, logger, "ret_95c_refresh_16ms", 95,
                         DRAM_TREFW_NS / 2U, 4, 1, "hot_2x_refresh_holds");

    dram_clear_faults(dram);
    dram_set_temperature(dram, 25);
    dram_refresh(dram);
    dram_set_odecc_enabled(dram, 1);

    if (!ok)
    {
        printf("[RESULT] FAIL: retention/refresh model did not behave as expected\n");
        return -1;
    }

    printf("[RESULT] PASS: refresh beats retention; above 85C refresh must double\n");
    return 0;
}

// CLI 단독 실행 모드: 판정(assert) 없이 돌리고 결과만 CSV로 남긴다.
// GUI가 "이 조건으로 한 번 돌려줘"를 시키게 될 창구
static int run_cli_constant(DramModel *dram, Logger *logger)
{
    MemoryTestResult result;
    int failed;

    dram_reset_ecc_stats(dram);
    failed = memory_test_constant_pattern(dram, REGION_START, REGION_LEN,
                                          REGION_PATTERN, &result) != 0;
    logger_row(logger, "cli_constant", failed ? "FAIL" : "PASS", REGION_START,
               REGION_LEN, REGION_PATTERN, &result, dram, "cli_single_run");
    printf("[RESULT] cli constant: %s (errors=%zu ecc_corr=%zu ecc_uncorr=%zu)\n",
           failed ? "FAIL" : "PASS", result.error_count,
           dram_ecc_correction_count(dram), dram_ecc_uncorrectable_count(dram));
    return 0;
}

static int run_cli_march(DramModel *dram, Logger *logger)
{
    MemoryTestResult result;
    int failed;

    dram_reset_ecc_stats(dram);
    failed = memory_test_march_c_minus(dram, REGION_START, REGION_LEN,
                                       &result) != 0;
    logger_row(logger, "cli_march_c_minus", failed ? "FAIL" : "PASS", REGION_START,
               REGION_LEN, 0, &result, dram, "cli_single_run");
    printf("[RESULT] cli march c-: %s (errors=%zu ecc_corr=%zu ecc_uncorr=%zu)\n",
           failed ? "FAIL" : "PASS", result.error_count,
           dram_ecc_correction_count(dram), dram_ecc_uncorrectable_count(dram));
    return 0;
}

// 이름 -> 시나리오 함수. --test tc7 처럼 하나만 골라 돌릴 수 있다
typedef struct TestEntry
{
    const char *name;
    int (*fn)(DramModel *dram, Logger *logger);
} TestEntry;

static const TestEntry kTests[] =
{
    { "tc1", tc1_address_decode },
    { "tc2", tc2_topology_pattern },
    { "tc3", tc3_basic_rw },
    { "tc4", tc4_constant_pattern },
    { "tc5", tc5_odecc_escape },
    { "tc6", tc6_stuck_escape },
    { "tc7", tc7_scrub_screen },
    { "tc8", tc8_double_bit },
    { "tc9", tc9_march_c_minus },
    { "tc10", tc10_coverage_matrix },
    { "tc11", tc11_retention_refresh },
};

static int run_selected(DramModel *dram, Logger *logger, const CliOptions *opt)
{
    size_t i;

    if (strcmp(opt->test_name, "constant") == 0)
    {
        return run_cli_constant(dram, logger);
    }

    if (strcmp(opt->test_name, "march") == 0)
    {
        return run_cli_march(dram, logger);
    }

    if (strcmp(opt->test_name, "all") == 0)
    {
        // 시나리오 목차 전체: 하나라도 실패하면 즉시 중단
        for (i = 0; i < sizeof(kTests) / sizeof(kTests[0]); i++)
        {
            if (kTests[i].fn(dram, logger) != 0)
            {
                return -1;
            }
        }
        return 0;
    }

    for (i = 0; i < sizeof(kTests) / sizeof(kTests[0]); i++)
    {
        if (strcmp(opt->test_name, kTests[i].name) == 0)
        {
            return kTests[i].fn(dram, logger);
        }
    }

    printf("[ERROR] Unknown test name: %s\n", opt->test_name);
    return -1;
}

int main(int argc, char **argv)
{
    DramModel dram;
    Logger logger;
    CliOptions opt;
    int parsed;
    int result;

    parsed = parse_args(argc, argv, &opt);
    if (parsed != 0)
    {
        print_usage(argv[0]);
        return (parsed > 0) ? 0 : 1;
    }

    printf("[BOOT] C-Based DRAM Validation Simulator\n");
    printf("[DRAM] Initializing virtual DRAM: %zu MB\n", opt.size_mb);

    if (logger_open(&logger, TEST_LOG_PATH) != 0)
    {
        printf("[ERROR] Failed to open test log: %s\n", TEST_LOG_PATH);
        return 1;
    }

    printf("[LOG] Writing test log to %s\n", TEST_LOG_PATH);

    if (dram_init(&dram, opt.size_mb * BYTES_PER_MB) != 0)
    {
        printf("[ERROR] Failed to initialize virtual DRAM\n");
        logger_close(&logger);
        return 1;
    }

    printf("[DRAM] Virtual DRAM initialized: %zu bytes\n", dram_size_bytes(&dram));
    print_dram_geometry(&dram);

    if (opt.inject_spec != NULL && apply_inject(&dram, opt.inject_spec) != 0)
    {
        printf("[ERROR] Bad --inject spec: %s\n", opt.inject_spec);
        dram_free(&dram);
        logger_close(&logger);
        return 1;
    }

    result = run_selected(&dram, &logger, &opt);

    dram_free(&dram);
    logger_close(&logger);
    printf("[DRAM] Virtual DRAM released\n");

    return (result != 0) ? 1 : 0;
}

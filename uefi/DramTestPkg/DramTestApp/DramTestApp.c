// dram_test.efi: the same validation core, now running before any OS.
// Reproduces the tc6 escape - a stuck-at bit that On-Die ECC hides,
// so the boot-time test passes while the cell stays broken
#include <Uefi.h>

#include "../../../core/dram_model.h"
#include "../../../core/memory_test.h"
#include "../../../core/dlog.h"

// host tc6과 같은 값 => 같은 결함, 같은 주소, 같은 패턴 = 같은 테스트
#define DEMO_DRAM_MB 16U
#define DEMO_REGION_START 0x2000U
#define DEMO_REGION_LEN (64U * 1024U)
#define DEMO_PATTERN 0xAAAAAAAAU
#define DEMO_STUCK_ADDR 0x4000U
#define DEMO_STUCK_MASK 0x00000002U

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    DramModel dram;
    MemoryTestResult result;
    const DramGeometry *geometry = NULL;
    int pass;
    int escaped;

    (void)ImageHandle;
    (void)SystemTable;

    dlog_printf("[BOOT] dram_test.efi - DDR5 validation core, pre-OS\n");

    if (dram_init(&dram, DEMO_DRAM_MB * 1024U * 1024U) != 0)
    {
        dlog_printf("[ERROR] dram_init failed\n");
        return EFI_OUT_OF_RESOURCES;
    }

    geometry = dram_geometry(&dram);
    dlog_printf("[DRAM] %u MB, [ROW(%u) | BG(%u) | BA(%u) | COL(%u)]\n",
                DEMO_DRAM_MB, geometry->row_bits, geometry->bg_bits,
                geometry->ba_bits, geometry->col_bits);

    dram_reset_ecc_stats(&dram);
    dram_add_stuck_fault(&dram, DRAM_FAULT_STUCK_AT_0,
                         DEMO_STUCK_ADDR, DEMO_STUCK_MASK);

    pass = memory_test_constant_pattern(&dram, DEMO_REGION_START,
                                        DEMO_REGION_LEN, DEMO_PATTERN,
                                        &result) == 0;

    dlog_printf("[ECC ] corrected=%zu uncorrectable=%zu\n",
                dram_ecc_correction_count(&dram),
                dram_ecc_uncorrectable_count(&dram));

    // tc6과 같은 escape 판정: 통과 + 검출 0 + 정정은 일어남
    escaped = pass &&
              result.error_count == 0 &&
              dram_ecc_correction_count(&dram) > 0;
    if (escaped)
    {
        dlog_printf("[RESULT] PASS: stuck-at escaped the test (hidden by On-Die ECC), at boot\n");
    }
    else
    {
        dlog_printf("[RESULT] FAIL: expected escape did not happen\n");
    }

    dram_free(&dram);
    dlog_printf("[DONE] dram_test.efi finished\n");
    return EFI_SUCCESS;
}

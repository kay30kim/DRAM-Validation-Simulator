// dram_test.efi: the same validation core, now running before any OS.
// Reproduces the tc6 escape - a stuck-at bit that On-Die ECC hides,
// so the boot-time test passes while the cell stays broken
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

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

static void wait_for_key(void)
{
    EFI_INPUT_KEY key;
    UINTN index;

    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &index);
    gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
}

// 시뮬레이터가 아니라 진짜 물리 메모리를 테스트한다. 실제 스크린 프로그램이
// pre-OS에서 하는 일 그대로: 펌웨어에게 메모리 지도를 받아 쓸 수 있는 영역을
// 파악하고, 물리 페이지를 할당해 패턴을 써보고 되읽어 확인한다
static void test_real_memory(void)
{
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN map_size = 0;
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINT32 desc_version = 0;
    UINTN usable_pages = 0;
    EFI_PHYSICAL_ADDRESS phys = 0;
    UINTN pages = 16; // 16 x 4KB = 64KB
    UINT32 *cells;
    UINTN words;
    UINTN bad = 0;
    UINTN i;

    gBS->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version);

    map_size += 2 * desc_size;
    // AllocatePool -> map_size Byte 크기만큼 버퍼 할당, map에 시작 주소 반환
    if (gBS->AllocatePool(EfiBootServicesData, map_size, (VOID **)&map) != EFI_SUCCESS)
    {
        return;
    }
    // GetMemoryMap -> 현재 UEFI 메모리 맵의 descriptor 목록을 map 버퍼에 채움
    if (gBS->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_version) != EFI_SUCCESS)
    {
        gBS->FreePool(map);
        return;
    }

    for (i = 0; i < map_size / desc_size; i++) // sizeof(EFI_MEMORY_DESCRIPTOR)로 하면 안 됨. 펌웨어가 더 크게 쓸 수 있다
    {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * desc_size); // sizeof(EFI_MEMORY_DESCRIPTOR)로 하면 안 됨. 펌웨어가 더 크게 쓸 수 있다

        if (d->Type == EfiConventionalMemory)
        {
            usable_pages += d->NumberOfPages;
        }
    }
    gBS->FreePool(map);

    dlog_printf("[MMAP] usable memory: %zu pages (%zu MB)\n",
                (size_t)usable_pages, (size_t)(usable_pages * 4096 / (1024 * 1024)));

    // AllocatePages -> 4KB 페이지 단위로 phys에 할당된 시작 주소 받음. (연속된 크기를 pages 수만큼 받음)
    if (gBS->AllocatePages(AllocateAnyPages, EfiBootServicesData, pages, &phys) != EFI_SUCCESS)
    {
        dlog_printf("[MMAP] AllocatePages failed\n");
        return;
    }

    cells = (UINT32 *)(UINTN)phys;
    words = pages * 4096 / sizeof(UINT32);
    for (i = 0; i < words; i++)
    {
        cells[i] = 0xA5A5A5A5U;
    }
    for (i = 0; i < words; i++)
    {
        if (cells[i] != 0xA5A5A5A5U)
        {
            bad++;
        }
    }

    dlog_printf("[TEST] real memory at 0x%zx: %s (%zu words, %zu bad)\n",
                (size_t)phys, bad == 0 ? "PASS" : "FAIL",
                (size_t)words, (size_t)bad);

    gBS->FreePages(phys, pages);
}

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

    test_real_memory();

    dlog_printf("[DONE] finished - press any key to exit\n");
    wait_for_key();
    return EFI_SUCCESS;
}

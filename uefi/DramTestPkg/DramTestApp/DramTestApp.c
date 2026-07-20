#include <Uefi.h>
#include <Library/UefiLib.h>

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    (void)ImageHandle;
    (void)SystemTable;

    Print(L"[BOOT] dram_test.efi skeleton - validation core comes next\n");
    return EFI_SUCCESS;
}

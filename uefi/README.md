# UEFI build

Builds the DRAM validation core as a standalone `.efi` that runs before any OS, under QEMU + OVMF.

Requires EDK2 at `~/edk2` and the toolchain from `scripts/setup_env.sh`.

## Build

```bash
cd ~/edk2
source edksetup.sh
export PACKAGES_PATH="$WORKSPACE:<repo>/uefi"
build -a X64 -t XCODE5 -b RELEASE -p DramTestPkg/DramTestPkg.dsc
```

Output: `~/edk2/Build/DramTestPkg/RELEASE_XCODE5/X64/DramTest.efi`

## Run

```bash
cp <build>/DramTest.efi uefi/esp/EFI/BOOT/BOOTX64.EFI
bash scripts/run_qemu.sh
```

Boots with no OS and prints:

```
[BOOT] dram_test.efi - DDR5 validation core, pre-OS
[DRAM] 16 MB, [ROW(9) | BG(3) | BA(2) | COL(10)]
[ECC ] corrected=4 uncorrectable=0
[RESULT] PASS: stuck-at escaped the test (hidden by On-Die ECC), at boot
[MMAP] usable memory: 129000 pages (503 MB)
[TEST] real memory at 0x6000000: PASS (16384 words, 0 bad)
[DONE] finished - press any key to exit
```

Two phases: the simulated escape on the DRAM model, then a real test on
physical memory the firmware handed out (`GetMemoryMap` + `AllocatePages`).

## Layout

- `DramTestPkg.dec` / `.dsc` — package and platform build files
- `DramTestApp/` — the application module (`UefiMain` entry point)
- `DramTestApp/core_*.c` — one-line `#include` of each `core/` source. EDK2
  can't list files outside the module folder, so the host core is pulled in
  and compiled unchanged.
- `DramTestApp/plat_uefi.c` + `string.h` — the `plat.h` implementation plus
  `memset`/`memcpy`, since EDK2 has no libc.

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

## Layout

- `DramTestPkg.dec` / `.dsc` — package and platform build files
- `DramTestApp/` — the application module (`UefiMain` entry point)

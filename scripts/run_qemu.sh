#!/usr/bin/env bash
# 빌드한 .efi를 QEMU + OVMF로 부팅한다. uefi/esp/ 가 가상 FAT 디스크가 된다.
set -e

EDK2_DIR="${EDK2_DIR:-$HOME/edk2}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESP="$ROOT/uefi/esp"
OVMF="$ROOT/uefi/ovmf/OVMF.fd"
APP="$EDK2_DIR/Build/DramTestPkg/RELEASE_XCODE5/X64/DramTest.efi"
[ -f "$APP" ] || APP="$EDK2_DIR/Build/MdeModule/RELEASE_XCODE5/X64/HelloWorld.efi"

mkdir -p "$ESP/EFI/BOOT"
cp "$APP" "$ESP/EFI/BOOT/BOOTX64.EFI"
echo "부팅: $(basename "$APP")  (종료: Ctrl-A 뒤 X)"

# q35: 최신 칩셋 흉내 / -bios: OVMF를 BIOS로 / -drive fat: esp 폴더를 FAT 디스크로
# -net none: 네트워크 끔 / -nographic: 창 없이 이 터미널로 입출력
qemu-system-x86_64 \
    -machine q35 \
    -m 512M \
    -bios "$OVMF" \
    -drive format=raw,file=fat:rw:"$ESP" \
    -net none \
    -nographic

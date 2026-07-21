#!/usr/bin/env bash
# macOS에서 EDK2 + QEMU + OVMF 세팅하고 HelloWorld까지 빌드해 툴체인을 확인한다.
# EDK2 위치 바꾸려면: EDK2_DIR=/경로 bash scripts/setup_env.sh
set -e

EDK2_DIR="${EDK2_DIR:-$HOME/edk2}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OVMF="$ROOT/uefi/ovmf/OVMF.fd"

brew install qemu nasm mtoc acpica

if [ ! -d "$EDK2_DIR/.git" ]; then
    git clone https://github.com/tianocore/edk2.git "$EDK2_DIR"
fi
cd "$EDK2_DIR"
git submodule update --init
make -C BaseTools

mkdir -p "$(dirname "$OVMF")"
[ -s "$OVMF" ] || curl -fL -o "$OVMF" https://retrage.github.io/edk2-nightly/bin/RELEASEX64_OVMF.fd

source edksetup.sh

# Apple Silicon: mtoc 경로가 /usr/local/bin 으로 박혀 있어서 실제 경로로 바꾼다
sed -i '' "s|/usr/local/bin/mtoc|$(which mtoc)|g" Conf/tools_def.txt

build -p MdeModulePkg/MdeModulePkg.dsc \
      -m MdeModulePkg/Application/HelloWorld/HelloWorld.inf \
      -a X64 -t XCODE5 -b RELEASE

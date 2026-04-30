# Building from source

## 1. Toolchain

* GCC AArch64 cross-compiler (`gcc-aarch64-linux-gnu`) — or build natively
  on an AArch64 host
* Python ≥ 3.10
* `mkimage` (a vendored copy ships under `edk2_port/misc/tools/`)
* Standard EDK2 build deps:

  ```bash
  sudo apt install build-essential nasm uuid-dev acpica-tools \
                   gcc-aarch64-linux-gnu python3 python3-pip
  ```

## 2. Clone third-party sources

These are **not** vendored in this repo and must be cloned next to the
overlay:

```bash
cd edk2_port

# EDK2 — pin to the version we tested against
git clone https://github.com/tianocore/edk2.git edk2
( cd edk2 && \
  git checkout 46548b1adac82211d8d11da12dd914f41e7aa775 && \
  git submodule update --init --depth=1 \
      MdeModulePkg/Library/BrotliCustomDecompressLib/brotli \
      MdePkg/Library/MipiSysTLib/mipisyst \
      MdePkg/Library/BaseFdtLib/libfdt \
      CryptoPkg/Library/OpensslLib/openssl \
      CryptoPkg/Library/MbedTlsLib/mbedtls )

git clone --depth=1 https://github.com/tianocore/edk2-non-osi.git
git clone --depth=1 https://github.com/tianocore/edk2-platforms.git
```

## 3. One-shot build

```bash
cd edk2_port
bash build_rock4d_uefi.sh
```

The script:

* Detects host architecture (AArch64 / x86_64) and sets `GCC_AARCH64_PREFIX`
* Rebuilds **BaseTools** locally (mandatory on AArch64 hosts — the prebuilt
  `bin/` shipped in EDK2 is x86_64)
* Applies GCC 10–13 compatibility patches
  (`-Wimplicit-function-declaration`, LTO removal, stack-protector workaround)
* Symlinks `/Scripts/GccBase.lds` (required by the EDK2 GCC linker script —
  it is hard-coded as an absolute path)
* Builds `Platform/Radxa/ROCK4D/ROCK4D.dsc` (~15–30 minutes)
* Packs **BL31 + EDK2 + DTB** into a FIT image
* Assembles the final 16 MB SPI NOR image as `rock4d-spi-edk2.img`

## 4. Flash the result

See [FLASHING.md](FLASHING.md) (use the freshly built `rock4d-spi-edk2.img`
in place of the prebuilt one).

## Manual build (if the script fails)

```bash
cd edk2_port

# BinWrappers MUST be in front (Trim path resolution depends on it)
export PATH=$PWD/edk2/BaseTools/BinWrappers/PosixLike:$PWD/edk2/BaseTools/Source/C/bin:$PATH
export EDK_TOOLS_PATH=$PWD/edk2/BaseTools
export WORKSPACE=$PWD/edk2-rockchip
export CONF_PATH=$WORKSPACE/Conf
export PACKAGES_PATH="$WORKSPACE:$PWD/devicetree:$PWD/edk2:$PWD/edk2-non-osi:$PWD/edk2-platforms:$PWD/edk2-rockchip-non-osi"
export GCC5_AARCH64_PREFIX=""                              # AArch64 host
# export GCC5_AARCH64_PREFIX="aarch64-linux-gnu-"          # x86_64 host
export GCC_AARCH64_PREFIX="$GCC5_AARCH64_PREFIX"
export PYTHONPATH=$PWD/edk2/BaseTools/Source/Python

make -C edk2/BaseTools CC=gcc CXX=g++ -j$(nproc)

sudo mkdir -p /Scripts
sudo ln -sfn $PWD/edk2/BaseTools/Scripts/GccBase.lds /Scripts/GccBase.lds

source edk2/edksetup.sh BaseTools

build -s -n$(nproc) -a AARCH64 -t GCC \
      -p Platform/Radxa/ROCK4D/ROCK4D.dsc \
      -b RELEASE -D FIRMWARE_VER=rk3576-rock4d-v0.1
```

Outputs:

* `edk2-rockchip/Build/ROCK4D/RELEASE_GCC/FV/BL33_AP_UEFI.Fv` — EDK2 UEFI body
* `edk2-rockchip/Build/ROCK4D/RELEASE_GCC/FV/NOR_FLASH_IMAGE.fd` — full Flash image

## Packaging the SPI image manually

```bash
cd edk2_port

# 1. Extract BL31 PT_LOAD segments
python3 misc/extractbl31.py ../binaries/bl31.elf

# 2. Stage FIT inputs
FV=edk2-rockchip/Build/ROCK4D/RELEASE_GCC/FV/BL33_AP_UEFI.Fv
cp $FV BL33_AP_UEFI.Fv
cp misc/rk3576_spl.dtb rk3576-rock-4d.dtb
touch bl32.bin
[ -f bl31_0x000f0000.bin ] || touch bl31_0x000f0000.bin

# 3. Build the FIT
sed 's,@DEVICE@,rk3576-rock-4d,g' misc/uefi_rk3576.its > rk3576-rock-4d_EFI.its
misc/tools/$(uname -m)/mkimage -f rk3576-rock-4d_EFI.its -E rk3576-rock-4d_EFI.itb

# 4. Assemble the SPI NOR image
dd if=/dev/zero bs=1M count=16 of=rock4d-spi-edk2.img
dd if=misc/rk3576_spi_nor_gpt.img of=rock4d-spi-edk2.img conv=notrunc
dd if=../binaries/u-boot-spl.bin   of=rock4d-spi-edk2.img bs=1K seek=32   conv=notrunc
dd if=rk3576-rock-4d_EFI.itb       of=rock4d-spi-edk2.img bs=1K seek=1024 conv=notrunc
```

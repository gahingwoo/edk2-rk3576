#!/bin/bash
# ROCK 4D UEFI 固件一键编译脚本
# 使用官方 Linux 内核设备树
# 架构: TPL (Rockchip) → SPL → ATF BL31 → UEFI BL33

set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${BLUE}[STEP]${NC} $1"; }

BUILD_ROOT="${PWD}/rock4d-uefi-build"
CORES=$(nproc)

# 检查依赖
check_dependencies() {
    log_step "检查依赖工具..."
    
    local tools=(git make gcc python3 dtc mkimage wget)
    local missing=()
    
    for tool in "${tools[@]}"; do
        if ! command -v "$tool" &>/dev/null; then
            missing+=("$tool")
        fi
    done
    
    # 检查交叉编译器
    if ! command -v aarch64-linux-gnu-gcc &>/dev/null; then
        missing+=("gcc-aarch64-linux-gnu")
    fi
    
    if [ ${#missing[@]} -gt 0 ]; then
        log_error "缺少工具: ${missing[*]}"
        echo ""
        echo "安装命令:"
        echo "  Ubuntu/Debian:"
        echo "    sudo apt update"
        echo "    sudo apt install git build-essential gcc-aarch64-linux-gnu \\"
        echo "                     device-tree-compiler u-boot-tools python3 \\"
        echo "                     python3-distutils python-is-python3 bison flex \\"
        echo "                     libssl-dev bc wget curl"
        echo ""
        exit 1
    fi
    
    log_info "所有依赖已满足"
}

# 准备工作区
setup_workspace() {
    log_step "准备工作目录..."
    
    mkdir -p "${BUILD_ROOT}"/{atf,edk2,linux-kernel,firmware,output}
    cd "${BUILD_ROOT}"
    
    log_info "工作目录: ${BUILD_ROOT}"
}

# 下载官方固件基础文件
download_base_firmware() {
    log_step "下载官方固件基础文件..."
    
    cd "${BUILD_ROOT}/firmware"
    
    local SPL_URL="https://dl.radxa.com/rock4/4d/images/rk3576_spl_loader.bin"
    local SPI_URL="https://dl.radxa.com/rock4/4d/images/rock-4d-spi-flash-image-g4738f85-20250520.img"
    
    if [ ! -f "rk3576_spl_loader.bin" ]; then
        log_info "下载 SPL Loader..."
        wget -q --show-progress -c "${SPL_URL}"
    fi
    
    if [ ! -f "rock-4d-spi-base.img" ]; then
        log_info "下载 SPI Flash 基础镜像..."
        wget -q --show-progress -c "${SPI_URL}" -O rock-4d-spi-base.img
    fi
    
    log_info "✓ 固件文件已就绪"
    cd "${BUILD_ROOT}"
}

# 获取官方设备树
get_official_dts() {
    log_step "获取官方 Linux 内核设备树..."
    
    cd "${BUILD_ROOT}/linux-kernel"
    
    if [ ! -d "linux-rockchip" ]; then
        log_info "克隆 Linux Rockchip 内核..."
        log_warn "需要下载完整源码以获取 dt-bindings (约 200MB)"
        
        git clone --depth=1 --single-branch --branch for-next \
            https://kernel.googlesource.com/pub/scm/linux/kernel/git/mmind/linux-rockchip \
            linux-rockchip
        
        cd linux-rockchip
    else
        cd linux-rockchip
        log_info "更新内核源码..."
        git pull
    fi
    
    # 验证 DTS 文件
    if [ ! -f "arch/arm64/boot/dts/rockchip/rk3576-rock-4d.dts" ]; then
        log_error "未找到 rk3576-rock-4d.dts!"
        log_info "可用的 RK3576 设备树:"
        find arch/arm64/boot/dts/rockchip -name "rk3576*.dts*" 2>/dev/null | sed 's/^/  /'
        exit 1
    fi
    
    # 验证 dt-bindings
    if [ ! -d "include/dt-bindings" ]; then
        log_error "缺少 dt-bindings 目录!"
        exit 1
    fi
    
    log_info "找到官方设备树: rk3576-rock-4d.dts"
    log_info "找到 dt-bindings"
    cd "${BUILD_ROOT}"
}

# 编译官方设备树
compile_official_dtb() {
    log_step "编译官方设备树..."
    
    cd "${BUILD_ROOT}/linux-kernel/linux-rockchip"
    
    local DTS_DIR="arch/arm64/boot/dts/rockchip"
    local DTS_FILE="${DTS_DIR}/rk3576-rock-4d.dts"
    
    log_info "编译 DTB: rk3576-rock-4d.dtb"
    
    # 使用 cpp 预处理 (处理 #include)
    # 添加所有必需的包含路径
    cpp -nostdinc \
        -I "${DTS_DIR}" \
        -I "arch/arm64/boot/dts" \
        -I "include" \
        -I "scripts/dtc/include-prefixes" \
        -undef \
        -x assembler-with-cpp \
        "${DTS_FILE}" | \
    dtc -I dts -O dtb -o "${BUILD_ROOT}/output/rk3576-rock-4d.dtb" -
    
    if [ -f "${BUILD_ROOT}/output/rk3576-rock-4d.dtb" ]; then
        local size=$(stat -c%s "${BUILD_ROOT}/output/rk3576-rock-4d.dtb")
        log_info "DTB 编译成功: $(( size / 1024 )) KB"
    else
        log_error "DTB 编译失败!"
        exit 1
    fi
    
    cd "${BUILD_ROOT}"
}

# 编译 ARM Trusted Firmware
build_atf() {
    log_step "编译 ARM Trusted Firmware (BL31)..."
    
    cd "${BUILD_ROOT}/atf"
    
    if [ ! -d "arm-trusted-firmware" ]; then
        log_info "克隆 ATF 仓库..."
        git clone --depth=1 -b master \
            https://github.com/ARM-software/arm-trusted-firmware.git
    fi
    
    cd arm-trusted-firmware
    
    log_info "编译 RK3576 BL31 (Release)..."
    make CROSS_COMPILE=aarch64-linux-gnu- \
         PLAT=rk3576 \
         DEBUG=0 \
         LOG_LEVEL=40 \
         bl31 -j${CORES}
    
    local BL31_ELF="build/rk3576/release/bl31/bl31.elf"
    
    if [ -f "${BL31_ELF}" ]; then
        cp "${BL31_ELF}" "${BUILD_ROOT}/output/bl31.elf"
        local size=$(stat -c%s "${BL31_ELF}")
        log_info "✓ BL31 编译成功: $(( size / 1024 )) KB"
    else
        log_error "BL31 编译失败!"
        exit 1
    fi
    
    cd "${BUILD_ROOT}"
}

# 编译 EDK2 UEFI
build_edk2() {
    log_step "编译 EDK2 UEFI 固件 (BL33)..."
    
    cd "${BUILD_ROOT}/edk2"
    
    if [ ! -d "edk2" ]; then
        log_info "克隆 EDK2 仓库 (这可能需要几分钟)..."
        git clone --recursive --depth=1 \
            https://github.com/tianocore/edk2.git
        cd edk2
        git submodule update --init --depth=1
    else
        cd edk2
    fi
    
    # 设置 EDK2 环境变量 (会覆盖 WORKSPACE)
    export WORKSPACE="${PWD}"
    export PACKAGES_PATH="${PWD}"
    export GCC5_AARCH64_PREFIX=aarch64-linux-gnu-
    
    log_info "初始化 EDK2 构建环境..."
    source edksetup.sh BaseTools
    
    # 构建 BaseTools
    if [ ! -f "BaseTools/Source/C/bin/GenFw" ]; then
        log_info "构建 BaseTools..."
        make -C BaseTools -j${CORES}
    fi
    
    log_info "编译 UEFI 固件 (RELEASE)..."
    build -a AARCH64 \
          -t GCC5 \
          -b RELEASE \
          -p ArmVirtPkg/ArmVirtQemu.dsc \
          -D TTY_TERMINAL \
          -D PURE_ACPI_BOOT \
          -n ${CORES}
    
    # 检查多个可能的输出路径
    local UEFI_FD=""
    local POSSIBLE_PATHS=(
        "Build/ArmVirtQemu-AARCH64/RELEASE_GCC5/FV/QEMU_EFI.fd"
        "Build/ArmVirtQemu-AArch64/RELEASE_GCC5/FV/QEMU_EFI.fd"
        "Build/ArmVirtQemuKernel-AARCH64/RELEASE_GCC5/FV/QEMU_EFI.fd"
    )
    
    for path in "${POSSIBLE_PATHS[@]}"; do
        if [ -f "${path}" ]; then
            UEFI_FD="${path}"
            break
        fi
    done
    
    if [ -n "${UEFI_FD}" ] && [ -f "${UEFI_FD}" ]; then
        # 使用 BUILD_ROOT 路径
        mkdir -p "${BUILD_ROOT}/output"
        cp "${UEFI_FD}" "${BUILD_ROOT}/output/uefi.fd"
        local size=$(stat -c%s "${UEFI_FD}")
        log_info "UEFI 编译成功: $(( size / 1024 / 1024 )) MB"
        log_info "输出: ${BUILD_ROOT}/output/uefi.fd"
    else
        log_error "UEFI 编译失败或找不到输出文件!"
        log_info "Build 目录内容:"
        find Build -name "*.fd" 2>/dev/null | head -n 10
        cd "${BUILD_ROOT}"
        exit 1
    fi
    
    # 返回到 BUILD_ROOT
    cd "${BUILD_ROOT}"
}

# 创建 FIT Image
create_fit_image() {
    log_step "创建 FIT Image..."
    
    # 使用 BUILD_ROOT 绝对路径
    cd "${BUILD_ROOT}/output"
    
    log_info "工作目录: $(pwd)"
    
    # 验证所有组件
    log_info "验证所需文件..."
    for file in bl31.elf uefi.fd rk3576-rock-4d.dtb; do
        if [ ! -f "$file" ]; then
            log_error "缺少文件: $file"
            log_info "目录内容:"
            ls -lh
            exit 1
        fi
    done
    
    log_info "生成 FIT 配置文件..."
    cat > uefi.its << 'EOF'
/dts-v1/;

/ {
    description = "ROCK 4D UEFI Firmware with Official DTS";
    #address-cells = <1>;

    images {
        atf {
            description = "ARM Trusted Firmware BL31";
            data = /incbin/("bl31.elf");
            type = "firmware";
            arch = "arm64";
            os = "arm-trusted-firmware";
            compression = "none";
            load = <0x00040000>;
            entry = <0x00040000>;
        };

        fdt {
            description = "ROCK 4D Device Tree (Official)";
            data = /incbin/("rk3576-rock-4d.dtb");
            type = "flat_dt";
            arch = "arm64";
            compression = "none";
            load = <0x0a100000>;
        };

        uefi {
            description = "UEFI Firmware BL33";
            data = /incbin/("uefi.fd");
            type = "firmware";
            arch = "arm64";
            os = "u-boot";
            compression = "none";
            load = <0x04000000>;
            entry = <0x04000000>;
        };
    };

    configurations {
        default = "config-1";
        
        config-1 {
            description = "ROCK 4D UEFI Boot Configuration";
            firmware = "atf";
            loadables = "uefi";
            fdt = "fdt";
        };
    };
};
EOF
    
    log_info "打包 FIT Image..."
    mkimage -f uefi.its -E uefi.itb
    
    if [ -f "uefi.itb" ]; then
        local size=$(stat -c%s "uefi.itb")
        log_info "FIT Image 创建成功: $(( size / 1024 / 1024 )) MB"
    else
        log_error "FIT Image 创建失败!"
        exit 1
    fi
    
    cd "${BUILD_ROOT}"
}

# 打包最终 SPI 固件
package_final_firmware() {
    log_step "打包最终 SPI Flash 固件..."
    
    cd "${BUILD_ROOT}/output"
    
    log_info "复制基础 SPI 镜像..."
    cp "${BUILD_ROOT}/firmware/rock-4d-spi-base.img" rock4d-uefi-spi.img
    
    log_info "写入 FIT Image (偏移: 0x200000 / 2MB)..."
    dd if=uefi.itb \
       of=rock4d-uefi-spi.img \
       bs=1M seek=2 conv=notrunc status=none
    
    local size=$(stat -c%s "rock4d-uefi-spi.img")
    log_info "最终固件: $(( size / 1024 / 1024 )) MB"
    
    # 计算校验和
    log_info "计算 SHA256 校验和..."
    sha256sum rock4d-uefi-spi.img > rock4d-uefi-spi.img.sha256
    
    cd "${BUILD_ROOT}"
}

# 生成刷写指南
generate_flash_guide() {
    log_step "生成刷写指南..."
    
    cat > "${BUILD_ROOT}/output/FLASH_GUIDE.md" << 'EOF'
# ROCK 4D UEFI 固件刷写指南

## 需要准备的文件
- `rock4d-uefi-spi.img` - 完整 SPI 固件 (16MB)
- `uefi.itb` - FIT 镜像 (ATF + UEFI + DTB)
- `bl31.elf` - ARM Trusted Firmware
- `uefi.fd` - UEFI 固件
- `rk3576-rock-4d.dtb` - 官方设备树

---

## 刷写方法

### 方法 1：先用 SD 卡测试下（推荐）
这样不会动 SPI Flash，出问题也能轻松恢复。

```bash
# 插入 SD 卡 (至少 32MB)，确认设备名，比如 /dev/sdb
lsblk

# 写入固件
sudo dd if=rock4d-uefi-spi.img of=/dev/sdX bs=4M status=progress
sync

# 验证
sha256sum -c rock4d-uefi-spi.img.sha256

# 插入 ROCK 4D，从 SD 卡启动
EOF
    
    log_info "✓ 刷写指南: ${BUILD_ROOT}/output/FLASH_GUIDE.md"
}

# 显示总结
show_summary() {
    echo ""
    log_info "=========================================="
    log_info "  编译完成!"
    log_info "=========================================="
    echo ""
    
    cd "${BUILD_ROOT}/output"
    
    echo "输出文件:"
    echo ""
    ls -lh *.{elf,fd,dtb,itb,img} 2>/dev/null | \
        awk '{printf "  %-30s %10s\n", $9, $5}'
    echo ""
    
    echo "主要文件:"
    echo "rock4d-uefi-spi.img  - 完整 SPI Flash 固件"
    echo "uefi.itb             - FIT Image (可单独更新)"
    echo "FLASH_GUIDE.md       - 详细刷写指南"
    echo ""
    
    echo "下一步:"
    echo "  1. 阅读 FLASH_GUIDE.md"
    echo "  2. SD 卡测试 (推荐):"
    echo "     sudo dd if=rock4d-uefi-spi.img of=/dev/sdX bs=4M"
    echo "  3. 连接串口 (1500000 baud):"
    echo "     sudo screen /dev/ttyUSB0 1500000"
    echo ""
    
    log_warn "生成总用时: $SECONDS 秒"
}

# 主流程
main() {
    local start_time=$SECONDS
    
    echo ""
    log_info "=========================================="
    log_info "  ROCK 4D UEFI 固件一键编译"
    log_info "  使用官方 Linux 内核设备树"
    log_info "=========================================="
    echo ""
    
    check_dependencies
    setup_workspace
    
    echo ""
    download_base_firmware
    
    echo ""
    get_official_dts
    
    echo ""
    compile_official_dtb
    
    echo ""
    build_atf
    
    echo ""
    build_edk2
    
    echo ""
    create_fit_image
    
    echo ""
    package_final_firmware
    
    echo ""
    generate_flash_guide
    
    SECONDS=$start_time
    show_summary
}

# 错误处理
trap 'log_error "编译失败于第 $LINENO 行"; exit 1' ERR

# 执行
main "$@"
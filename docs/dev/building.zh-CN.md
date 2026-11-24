# 构建

[English](building.md) | 简体中文

怎样从源码构建固件映像。写到板子上的方法见[刷写固件](../flash.zh-CN.md)。映像偏移和 FIT 见[映像布局](image-layout.zh-CN.md)。

```bash
git clone --recurse-submodules https://github.com/gahingwoo/edk2-rk3576
cd edk2-rk3576

scripts/setup-host.sh          # once: packages, EDK2, dependency trees, BaseTools
scripts/build.sh   cm5io       # compile
scripts/package.sh cm5io       # -> out/CM5IO/CM5IO-sdcard.img
```

板子定义在 [`boards/`](https://github.com/gahingwoo/edk2-rk3576/tree/main/boards) 里：`rock4d`（Radxa ROCK 4D）、`cm5io`（ArmSoM CM5-IO）和 `tspi3m`（立创泰山派 3M）。

`scripts/build.sh <board> clean` 会先清空 `Build/` 和 `Conf/`。

每次 push，CI 都会先获取下文介绍的设备树头文件镜像，再对每块板子运行这三个脚本。工作流文件是 [`.github/workflows/build.yml`](https://github.com/gahingwoo/edk2-rk3576/blob/main/.github/workflows/build.yml)。

## 每个脚本做什么

编译从不安装软件包，也不往代码树之外写东西；只有 `setup-host.sh` 会这样做。

| 脚本 | 做什么 | 是否需要 sudo |
|---|---|---|
| `setup-host.sh` | apt 软件包、固定在 edk2-stable202602 的 EDK2、三个依赖代码树、核心补丁、BaseTools、`/Scripts/GccBase.lds` | 需要，会先询问 |
| `build.sh` | 重新生成 `Conf/`，修补 `tools_def.txt`，运行 `build` | 不需要 |
| `package.sh` | 提取 BL31 各段，构建 FIT，写出刷写映像 | 不需要 |

## 依赖

| 代码树 | 来源 |
|---|---|
| `third_party/edk2` | 子模块，由 `setup-host.sh` 固定到 edk2-stable202602（`b7a715f`）。 |
| `edk2-non-osi` | [tianocore/edk2-non-osi](https://github.com/tianocore/edk2-non-osi) |
| `edk2-platforms` | [tianocore/edk2-platforms](https://github.com/tianocore/edk2-platforms) |
| `edk2-rockchip-non-osi` | [edk2-porting/edk2-rk3588](https://github.com/edk2-porting/edk2-rk3588) 里的一个目录，用稀疏检出获取。其中的 AMD GOP 驱动会进入映像，所以它不是可选的。 |

`setup-host.sh` 把后三个克隆到 `third_party/`。想放在别处，设置 `DEPS_DIR` 即可；`build.sh` 读取同一个变量。

### EDK2 核心补丁

EDK2 核心的补丁放在 [`patches/`](https://github.com/gahingwoo/edk2-rk3576/tree/main/patches) 里，由 `setup-host.sh` 打上。一共 19 个：固件需要的修复（解压前刷新 FD、RELEASE 构建出错时打印 ESR/FAR、SD/MMC 超时），简体中文字符串以及首页和弹窗的改动，还有两个 UART 跟踪补丁，只在设置 `EDK2_CORE_DEBUG_PATCHES=1` 时才打上。每个补丁的说明见 [patches/README.md](https://github.com/gahingwoo/edk2-rk3576/blob/main/patches/README.zh-CN.md)。

不打这些补丁，构建照样能产出映像，但映像会出问题。打补丁会修改子模块，所以构建出的映像在报告版本时带 `-dirty` 后缀。

### 设备树头文件镜像

编译板级 DTS 需要上游内核的 `dt-bindings` 头文件和 Rockchip DTS 源文件，放在 `devicetree/mainline/upstream/`。`setup-host.sh` 不会获取它们。CI 用的方法是：

```bash
git clone --depth=1 --filter=blob:none --sparse \
    https://github.com/torvalds/linux /tmp/linux
git -C /tmp/linux sparse-checkout set \
    include/dt-bindings arch/arm64/boot/dts/rockchip include/uapi/linux
mkdir -p devicetree/mainline/upstream/src/arm64
ln -sfn /tmp/linux/include devicetree/mainline/upstream/include
ln -sfn /tmp/linux/arch/arm64/boot/dts/rockchip \
        devicetree/mainline/upstream/src/arm64/rockchip
```

`RockchipPkg.dec` 在 `[Includes]` 里列出了这两个目录，所以缺了它们时，构建会很早就失败，并给出清楚的错误。

## 不要只看退出码

`build.sh` 除了检查退出状态，还会在日志里查找编译错误。如果你手动运行 `build`，也要在日志里 grep 一下。

## 主机注意事项

* **AArch64 主机**：上游 EDK2 里的 BaseTools 二进制是 x86_64 的。`setup-host.sh` 会检测到不匹配，并用主机编译器重新编译它们。
* **GCC 10 到 13**：有几个警告变成了错误，而且 `-flto` 和 `-fstack-protector` 都会破坏独立运行（freestanding）的 EDK2 链接。`scripts/lib/tools_def_patch.py` 处理这三个问题，每次构建都从模板重新生成 `Conf/tools_def.txt`，所以重复运行结果不变。
* **`PATH`**：`BinWrappers/PosixLike` 必须排在最前面。这些包装脚本通过相对于 `BASH_SOURCE` 的路径找到同目录的其他文件，所以通过别处的符号链接调用它们会让 `Trim` 出错。
* **时钟偏差**：如果主机时钟比文件时间戳慢，`make` 会跳过本该重新编译的内容，并且照样以 0 退出。检查映像里的构建标记，或者 `touch` 你修改过的文件。

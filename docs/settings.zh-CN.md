# 设置项参考

[English](settings.md) | 简体中文

所有保存在 UEFI 变量里的设置项：所在菜单、变量和各板子的默认值。表格由 `scripts/gen-docs`
根据每次构建写出的 `features.json`（见 [features-json](dev/features-json.md)）生成；和代码不一致时 CI 会失败。
各选项的作用见[固件菜单](firmware-menus.zh-CN.md)。

"隐藏"表示这块板子的构建把选项隐藏了（`Pcd<X>Supported` 为 FALSE）。"@n" 是一个变量里装多个选项时，该选项的字节偏移。

## 选项

<!-- generated:settings -->

| 菜单 | 设置项 | 变量 | 取值 | CM5-IO | ROCK 4D | 泰山派 3M |
|---|---|---|---|---|---|---|
| Device Manager > EFI Memory Attribute Protocol | 启用协议 | `MemoryAttributeManagerData` | Disabled, Enabled | Enabled | Enabled | Enabled |
| Device Manager > Network Stack Configuration | 网络协议栈 | `NetworkStackConfigData` @0 | Disabled, Enabled | Enabled | Enabled | Enabled |
| Device Manager > Network Stack Configuration | IPv4 协议栈 | `NetworkStackConfigData` @1 | Disabled, Enabled | Enabled | Enabled | Enabled |
| Device Manager > Network Stack Configuration | IPv6 协议栈 | `NetworkStackConfigData` @2 | Disabled, Enabled | Disabled | Disabled | Disabled |
| Device Manager > Network Stack Configuration | PXE 启动 | `NetworkStackConfigData` @3 | Disabled, Enabled | Enabled | Enabled | Enabled |
| Device Manager > Network Stack Configuration | HTTP 启动 | `NetworkStackConfigData` @4 | Disabled, Enabled | Disabled | Disabled | Disabled |
| RK3576 Platform Configuration | CPU 最高频率 | `CpuMaxFreq` | 1608 MHz, 最高（2208 / 2016 MHz） | 隐藏 | 1608 MHz | 1608 MHz |
| RK3576 Platform Configuration | 702 MHz 用的 GPU 电压（775 mV） | `GpuRail775` | 关（750 mV）, 开（775 mV） | 隐藏 | 关（750 mV） | 开（775 mV） |
| RK3576 Platform Configuration | 截图保存位置 | `ScreenshotTarget` | 自动（U盘优先）, 仅 U盘, 仅 SD 卡, 仅内部存储 | 自动（U盘优先） | 自动（U盘优先） | 自动（U盘优先） |
| RK3576 Platform Configuration | 网络对时 | `NetworkTimeSync` | 禁用, 启用 | 启用 | 启用 | 启用 |
| RK3576 Platform Configuration | 将系统修改的变量保存到 eMMC | `RtVarEmmcCommit` | 禁用, 启用 | 禁用 | 禁用 | 禁用 |
| RK3576 Platform Configuration > ACPI / Device Tree | 系统配置表模式 | `ConfigTableMode` | ACPI, 设备树, 两者都提供 | 两者都提供 | 两者都提供 | 两者都提供 |
| RK3576 Platform Configuration > ACPI / Device Tree | PCIe ECAM 兼容模式 | `AcpiPcieEcamCompatMode` | 自动, 单设备, NXPMX6, AMAZON GRAVITON | 自动 | 自动 | 自动 |
| RK3576 Platform Configuration > ACPI / Device Tree | 风扇模式 | `AcpiFanControl` | 常开, 性能, 均衡, 静音, 关闭 | 常开 | 常开 | 常开 |
| RK3576 Platform Configuration > ACPI / Device Tree | CPU 空闲状态 | `AcpiCpuIdleStates` | 禁用, 启用 | 禁用 | 禁用 | 禁用 |
| RK3576 Platform Configuration > ACPI / Device Tree | NPU（实验性） | `AcpiNpu` | 禁用, 启用 | 启用 | 启用 | 启用 |
| RK3576 Platform Configuration > ACPI / Device Tree | 视频解码器（VDPU383，实验性） | `AcpiVdec` | 禁用, 启用 | 启用 | 启用 | 启用 |
| RK3576 Platform Configuration > ACPI / Device Tree | USB-C DisplayPort（实验性） | `UsbcDp` | 禁用, 启用 | 禁用 | 隐藏 | 禁用 |
| RK3576 Platform Configuration > ACPI / Device Tree | SDIO Wi-Fi（实验性） | `AcpiSdioWifi` | 禁用, 启用 | 隐藏 | 隐藏 | 禁用 |
| RK3576 Platform Configuration > ACPI / Device Tree | 40针 UART（实验性） | `Pin40Uart` | 禁用, 启用 | 隐藏 | 禁用 | 禁用 |
| RK3576 Platform Configuration > ACPI / Device Tree | 40针 GPIO/I2C/SPI 供应用使用（实验性） | `Pin40RhProxy` | 禁用, 启用 | 隐藏 | 禁用 | 禁用 |
| RK3576 Platform Configuration > ACPI / Device Tree | 硬件看门狗（实验性） | `HwWatchdog` | 禁用, 启用 | 禁用 | 禁用 | 禁用 |
| RK3576 Platform Configuration > ACPI / Device Tree | 电源键（RK806，实验性） | `AcpiPwrKey` | 禁用, 启用 | 禁用 | 禁用 | 隐藏 |
| RK3576 Platform Configuration > ACPI / Device Tree | 板载指示灯交给操作系统（实验性） | `AcpiLeds` | 禁用, 启用 | 隐藏 | 禁用 | 禁用 |
| RK3576 Platform Configuration > ACPI / Device Tree | ADC 按键交给操作系统（实验性） | `AcpiSaradc` | 禁用, 启用 | 隐藏 | 隐藏 | 启用 |
| RK3576 Platform Configuration > ACPI / Device Tree | 兼容模式 | `FdtCompatMode` | 厂商, 主线 | 主线 | 主线 | 主线 |
| RK3576 Platform Configuration > ACPI / Device Tree | 强制使用 UEFI GOP 显示 | `FdtForceGop` | 禁用, 启用 | 禁用 | 禁用 | 禁用 |
| RK3576 Platform Configuration > ACPI / Device Tree | 支持替换 DTB 和 overlay | `FdtSupportOverrides` | 禁用, 启用 | 禁用 | 禁用 | 禁用 |
| RK3576 Platform Configuration > ACPI / Device Tree | 固件修正 | `FdtOverrideFixup` | 禁用, 启用 | 启用 | 启用 | 启用 |
| RK3576 Platform Configuration > ACPI / Device Tree | 首选基础 DTB 路径 | `FdtOverrideBasePath` @0 | 最多 254 个字符 | "" | "" | "" |
| RK3576 Platform Configuration > ACPI / Device Tree | 首选 overlay 路径 | `FdtOverrideOverlayPath` @0 | 最多 254 个字符 | "" | "" | "" |
| RK3576 Platform Configuration > Display | 显示模式 | `DisplayModePreset` | 原生模式（显示器首选）, 自定义模式, 640x480 @ 60 Hz, ... (共 28 项) | 原生模式（显示器首选） | 原生模式（显示器首选） | 原生模式（显示器首选） |
| RK3576 Platform Configuration > Display | 像素时钟 (kHz) | `DisplayModeCustom` @4 | 25175 到 600000 | 25175 | 25175 | 25175 |
| RK3576 Platform Configuration > Display | 水平有效像素 | `DisplayModeCustom` @8 | 640 到 4096 | 640 | 640 | 640 |
| RK3576 Platform Configuration > Display | 水平前沿 | `DisplayModeCustom` @12 | 1 到 2048 | 16 | 16 | 16 |
| RK3576 Platform Configuration > Display | 水平同步宽度 | `DisplayModeCustom` @16 | 1 到 2048 | 96 | 96 | 96 |
| RK3576 Platform Configuration > Display | 水平后沿 | `DisplayModeCustom` @20 | 1 到 2048 | 48 | 48 | 48 |
| RK3576 Platform Configuration > Display | 水平同步极性 | `DisplayModeCustom` @24 | -, + | - | - | - |
| RK3576 Platform Configuration > Display | 垂直有效行数 | `DisplayModeCustom` @28 | 480 到 4320 | 480 | 480 | 480 |
| RK3576 Platform Configuration > Display | 垂直前沿 | `DisplayModeCustom` @32 | 1 到 1024 | 10 | 10 | 10 |
| RK3576 Platform Configuration > Display | 垂直同步宽度 | `DisplayModeCustom` @36 | 1 到 1024 | 2 | 2 | 2 |
| RK3576 Platform Configuration > Display | 垂直后沿 | `DisplayModeCustom` @40 | 1 到 1024 | 33 | 33 | 33 |
| RK3576 Platform Configuration > Display | 垂直同步极性 | `DisplayModeCustom` @44 | -, + | - | - | - |
| RK3576 Platform Configuration > Display | 强制输出 | `DisplayForceOutput` | 禁用, 启用 | 启用 | 启用 | 启用 |
| RK3576 Platform Configuration > Display | 复制输出 | `DisplayDuplicateOutput` | 禁用, 启用 | 禁用 | 禁用 | 禁用 |
| RK3576 Platform Configuration > Display | 屏幕旋转 | `DisplayRotation` | 默认, 0°（不旋转）, 顺时针 90°, 顺时针 270° | 默认 | 默认 | 默认 |
| RK3576 Platform Configuration > Display | 屏幕亮度 (%) | `DisplayBrightness` | 10 到 100 | 60 | 60 | 60 |
| RK3576 Platform Configuration > Display | HDMI 信号模式 | `HdmiSignalingMode` | 自动, DVI, HDMI | 自动 | 自动 | 自动 |
| RK3576 Platform Configuration > ComboPHY Mode | ComboPHY0 (M.2 PCIe) | `ComboPhy0Mode` | 未连接, PCIe | PCIe | PCIe | PCIe |
| RK3576 Platform Configuration > ComboPHY Mode | ComboPHY1 (USB Type-A) | `ComboPhy1Mode` | 未连接, PCIe, USB 3.0 | USB 3.0 | USB 3.0 | USB 3.0 |

<!-- /generated -->

## 变量

<!-- generated:settings-variables -->

| 变量 | GUID | 属性 | 大小 |
|---|---|---|---|
| `MemoryAttributeManagerData` | `efab3427-4793-4e9e-aa29-880c9a775b5f` | NV, BS | 1 |
| `NetworkStackConfigData` | `663413e7-ed00-41f6-a824-a988d0459dc8` | NV, BS | 5 |
| `CpuMaxFreq` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `GpuRail775` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `ScreenshotTarget` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `NetworkTimeSync` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `RtVarEmmcCommit` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `ConfigTableMode` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 4 |
| `AcpiPcieEcamCompatMode` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 4 |
| `AcpiFanControl` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `AcpiCpuIdleStates` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `AcpiNpu` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `AcpiVdec` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `UsbcDp` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `AcpiSdioWifi` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `Pin40Uart` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `Pin40RhProxy` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `HwWatchdog` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `AcpiPwrKey` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `AcpiLeds` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `AcpiSaradc` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `FdtCompatMode` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 4 |
| `FdtForceGop` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `FdtSupportOverrides` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `FdtOverrideFixup` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `FdtOverrideBasePath` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 510 |
| `FdtOverrideOverlayPath` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 510 |
| `DisplayModePreset` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 4 |
| `DisplayModeCustom` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 56 |
| `DisplayForceOutput` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `DisplayDuplicateOutput` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `DisplayRotation` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 2 |
| `DisplayBrightness` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `HdmiSignalingMode` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 1 |
| `ComboPhy0Mode` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 4 |
| `ComboPhy1Mode` | `8b4f2e3d-ac5e-4f9b-c2d6-3e7e9f1b4d8c` | NV, BS, RT | 4 |

<!-- /generated -->

属性：NV 非易失，BS 启动服务，RT 运行时。操作系统可以改 RT 变量；在 eMMC 板子上，只有打开
"Save OS Variable Changes to eMMC" 才会保存这种改动（见[固件菜单](firmware-menus.zh-CN.md#操作系统写入的启动项)）。

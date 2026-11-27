# 板子功能

[English](BOARD-FEATURES.md) | 简体中文

每块板子的固件构建里包含什么，由 `scripts/gen-docs` 根据构建写出的 `features.json`
（见 [features-json](dev/features-json.md)）生成；本页和代码不一致时 CI 会失败。这里说的是构建了什么，
不是跑过什么：板上结果见各板子页面（[ROCK 4D](boards/rock4d.zh-CN.md)、[CM5-IO](boards/cm5io.zh-CN.md)、
[泰山派 3M](boards/tspi3m.zh-CN.md)）。

"有，X" 给出默认值；设置项那几行给出默认值，构建里不提供该选项时写"隐藏"。

<!-- generated:feature-matrix -->

| 功能 | CM5-IO | ROCK 4D | 泰山派 3M |
|---|---|---|---|
| Windows（ACPI 表） | 有 | 有 | 有 |
| Armbian (U-Boot) 一次启动 | 有 | 有 | 有 |
| HDMI 输出 | 有 | 有 | 有 |
| MIPI DSI 屏 | 无 | 无 | 有 |
| USB-C DisplayPort | 有，关 | 无 | 有，关 |
| CPU 空闲状态 | 有，关 | 有，关 | 有，关 |
| 风扇控制 | 有，常开 | 有，常开 | 有，常开 |
| 网络启动（PXE） | 有，开 | 有，开 | 有，开 |
| HTTP 启动 | 有，关 | 有，关 | 有，关 |
| 安全启动 | 有 | 有 | 有 |
| CPU 最高频率 (`PcdCpuMaxFreqSupported`) | 1608 MHz | 1608 MHz | 1608 MHz |
| GPU 电压 (`PcdGpuRail775Supported`) | 750 mV（594 MHz） | 750 mV（594 MHz） | 775 mV（702 MHz） |
| 风扇模式 (`PcdAcpiFanControlSupported`) | 常开 | 常开 | 常开 |
| NPU（实验性） (`AcpiNpuSupported`) | 启用 * | 启用 * | 启用 * |
| USB-C DisplayPort（实验性） (`PcdUsbcDpSupported`) | 禁用 | 隐藏 | 禁用 |
| SDIO Wi-Fi（实验性） (`PcdAcpiSdioWifiSupported`) | 禁用 | 隐藏 | 禁用 |
| 40针 UART（实验性） (`PcdPin40Supported`) | 禁用 | 禁用 | 禁用 |
| 40针 GPIO/I2C/SPI 供应用使用 (`PcdPin40Supported`) | 禁用 | 禁用 | 禁用 |
| 电源键（实验性） (`PcdAcpiPwrKeySupported`) | 禁用 | 禁用 | 隐藏 |
| 板载指示灯（实验性） (`PcdAcpiLedsSupported`) | 禁用 | 禁用 | 禁用 |
| ADC 按键（实验性） (`PcdAcpiSaradcSupported`) | 隐藏 | 隐藏 | 启用 |

\* 只有 RK3576Dxe 在启动时设置了这个变量才显示（启动时探测，不是构建设置）

<!-- /generated -->

更多：

- 每个设置项、它的变量和各板子的默认值：[设置项参考](settings.zh-CN.md)；
- 各板子的 ACPI 设备以及由什么决定：[ACPI](dev/acpi.zh-CN.md)；
- Windows 驱动：[woa-rk3576](https://github.com/gahingwoo/woa-rk3576)；
- 2026-11-25 手写的功能清单（含各板子的 ACPI、驱动表和测试情况）：[历史](history/2026-11-26-board-features.md)。

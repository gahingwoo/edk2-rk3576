# EDK2 UEFI for Rockchip RK3576 based SBC
# Radxa ROCK 4D UEFI Firmware Builder

[English](#english) | [中文](#chinese)

---

<a name="english"></a>
## English

### What is this?

 This is a one-click build script for compiling UEFI firmware for the ROCK 4D (RK3576) single-board computer. Instead of the default U-Boot bootloader, you'll get a proper UEFI implementation that can boot standard ARM64 operating systems.

 👉Project Github homepage: [https://github.com/gahingwoo/edk2-rk3576/](https://github.com/gahingwoo/edk2-rk3576/)

### Why would I want this?

- **Standard boot process**: Use UEFI like modern PCs
- **Better OS support**: Run standard ARM64 distros without custom kernels

### Quick Start

#### You can find prebuilt images in the [release section](https://github.com/gahingwoo/edk2-rk3576/releases).

```bash
# 1. Download the build script
wget https://github.com/gahingwoo/edk2-rk3576/blob/main/build.sh
chmod +x build.sh

# 2. Run it (grab a coffee, this takes 30-60 minutes first time)
./build.sh

# 3. Find your firmware
cd rock4d-uefi-build/output/
ls -lh rock4d-uefi-spi.img
```

### What you need

**Hardware:**
- ROCK 4D board
- microSD card (32MB minimum for testing)
- USB-TTL adapter (for serial console)
- USB Type-C cable

**Software:**
```bash
sudo apt install git build-essential gcc-aarch64-linux-gnu \
                 device-tree-compiler u-boot-tools python3 \
                 python3-distutils python-is-python3 bison flex \
                 libssl-dev bc wget curl
```

### How to flash

#### Option 1: SD Card Test (Recommended for first try)

This is the safest way - it won't touch your SPI flash at all.

```bash
# Write to SD card
sudo dd if=rock4d-uefi-spi.img of=/dev/sdX bs=4M status=progress
sync

# Pop it into your ROCK 4D and power on
```

The board will boot from the SD card automatically. If something goes wrong, just remove the SD card and you're back to normal!

#### Option 2: Flash to SPI (Permanent)

**Warning**: This will overwrite your SPI flash. Make sure you know what you're doing!

**Step 1: Enter Maskrom mode**

1. Power off your ROCK 4D
2. Hold down the **Maskrom button** (near the 40-pin header)
3. Connect USB Type-C to your computer
4. Power on while holding the button
5. Release the button after 2-3 seconds

Check if it worked:
```bash
lsusb | grep 2207
# You should see: ID 2207:350e Fuzhou Rockchip Electronics
```

**Step 2: Flash the firmware**

```bash
# Install the tool
sudo apt install rkdeveloptool

# Flash it
sudo rkdeveloptool db firmware/rk3576_spl_loader.bin
sudo rkdeveloptool wl 0 rock4d-uefi-spi.img
sudo rkdeveloptool rd
```

### Serial Console

Connect your USB-TTL adapter to these pins:

```
Pin 8  (UART0_TX) → USB-TTL RX
Pin 10 (UART0_RX) → USB-TTL TX
Pin 6  (GND)      → USB-TTL GND
```

Then connect:
```bash
sudo screen /dev/ttyUSB0 1500000
```

**Yes, that's 1.5 million baud!** Not a typo.

### What you should see

If everything works, you'll see something like:

```
U-Boot TPL 2024.07
Channel 0: LPDDR5, 2112MHz
...
U-Boot SPL 2024.07
...
NOTICE:  BL31: v2.11.0
...
UEFI firmware (version 2.70)
Press ESCAPE for boot options...

UEFI Interactive Shell v2.2
Shell> _
```

Congratulations! You're now in the UEFI Shell.

### Troubleshooting

**Nothing on serial console?**
- Double-check TX/RX aren't swapped
- Make sure baud rate is 1500000
- Try pressing Enter a few times

**Stuck at U-Boot?**
- Your FIT image might be corrupted
- Try rebuilding with `./build.sh`
- Check the sha256sum of your firmware

**Can't boot?**
- Your hardware might not like this firmware (yet)
- Flash back the official firmware and report the issue

### Going back to stock

Don't worry, it's easy:

```bash
sudo rkdeveloptool db firmware/rk3576_spl_loader.bin
sudo rkdeveloptool wl 0 firmware/rock-4d-spi-base.img
sudo rkdeveloptool rd
```

Alternatively, you can use the GUI to flash the application. [RKDevelopToolGUI](https://github.com/gahingwoo/RKDevelopTool-GUI/)

### What's next?

- Install a proper OS (Ubuntu, Debian, Fedora ARM64)
- Test USB booting
- Check if NVMe works
- Help improve ACPI tables
- Report bugs

### Contributing

Found a bug? Got it working on different hardware? Want to improve something?

Open an issue or send a pull request! I'd love to hear from you.

### Credits

- **Rockchip** - For the RK3576 SoC
- **Radxa** - For the ROCK 4D board
- **ARM** - For Trusted Firmware
- **TianoCore** - For EDK2 UEFI implementation
- **Linux community** - For device tree sources

### License

This build script is MIT licensed. The firmware components have their own licenses:
- ATF: BSD-3-Clause
- EDK2: BSD-2-Clause-Patent
- U-Boot: GPL-2.0

---

<a name="chinese"></a>
## 中文

### 这是什么？

这是一个一键编译 瑞莎 ROCK 4D（RK3576）单板计算机 UEFI 固件的脚本。不用默认的 U-Boot 引导加载器，你可以得到一个标准的 UEFI 实现，可以启动标准的 ARM64 操作系统。

项目Github主页: [https://github.com/gahingwoo/edk2-rk3576/](https://github.com/gahingwoo/edk2-rk3576/)

### 为什么我需要这个？

- **标准启动流程**：像现代 PC 一样使用 UEFI
- **更好的系统支持**：运行标准 ARM64 发行版，不需要定制内核

### 快速开始
预编译镜像在 [Release](https://github.com/gahingwoo/edk2-rk3576/releases) 页面，可直接下载。
```bash
# 1. 下载编译脚本
wget https://github.com/gahingwoo/edk2-rk3576/blob/main/build.sh
chmod +x build.sh

# 2. 运行（冲杯茶，第一次需要 5-10 分钟）
./build.sh

# 3. 找到你的固件
cd rock4d-uefi-build/output/
ls -lh rock4d-uefi-spi.img
```

### 你需要什么

**硬件：**
- ROCK 4D 开发板
- microSD 卡（测试至少需要 32MB）
- USB-TTL 适配器（用于串口控制台）
- USB Type-C 线

**软件：**
```bash
sudo apt install git build-essential gcc-aarch64-linux-gnu \
                 device-tree-compiler u-boot-tools python3 \
                 python3-distutils python-is-python3 bison flex \
                 libssl-dev bc wget curl
```

### 怎么刷写

#### 方案 1：SD 卡测试（首次推荐）

这是最安全的方式 - 完全不会碰你的 SPI flash。

```bash
# 写入 SD 卡
sudo dd if=rock4d-uefi-spi.img of=/dev/sdX bs=4M status=progress
sync

# 插入 ROCK 4D 然后开机
```

开发板会自动从 SD 卡启动。如果出问题了，拔掉 SD 卡就恢复正常了！

#### 方案 2：刷入 SPI（永久性）

**警告**：这会覆盖你的 SPI flash。确保你知道自己在做什么！

**步骤 1：进入 Maskrom 模式**

1. 关闭 ROCK 4D 电源
2. 按住 **Maskrom 按钮**（靠近 40 针排针）
3. 连接 USB Type-C 到电脑
4. 按住按钮的同时上电
5. 2-3 秒后松开按钮

检查是否成功：
```bash
lsusb | grep 2207
# 应该看到：ID 2207:350e Fuzhou Rockchip Electronics
```

**步骤 2：刷写固件**

```bash
# 安装工具
sudo apt install rkdeveloptool

# 刷写
sudo rkdeveloptool db firmware/rk3576_spl_loader.bin
sudo rkdeveloptool wl 0 rock4d-uefi-spi.img
sudo rkdeveloptool rd
```

### 串口控制台

把 USB-TTL 适配器连接到这些针脚：

```
Pin 8  (UART0_TX) → USB-TTL RX
Pin 10 (UART0_RX) → USB-TTL TX
Pin 6  (GND)      → USB-TTL GND
```

然后连接：
```bash
sudo screen /dev/ttyUSB0 1500000
```

**对，就是 150 万波特率！** 不是笔误。

### 你应该看到什么

如果一切正常，你会看到类似这样的输出：

```
U-Boot TPL 2024.07
Channel 0: LPDDR5, 2112MHz
...
U-Boot SPL 2024.07
...
NOTICE:  BL31: v2.11.0
...
UEFI firmware (version 2.70)
Press ESCAPE for boot options...

UEFI Interactive Shell v2.2
Shell> _
```

恭喜！你现在进入 UEFI Shell 了。

### 故障排除

**串口没有输出？**
- 再次检查 TX/RX 没有接反
- 确保波特率是 1500000
- 试着按几次回车

**卡在 U-Boot？**
- 你的 FIT 镜像可能损坏了
- 试试重新用 `./build.sh` 编译
- 检查固件的 sha256sum

**无法启动？**
- 你的硬件可能（暂时）不支持这个固件
- 刷回官方固件然后报告问题

### 恢复官方固件

别担心，很简单：

```bash
sudo rkdeveloptool db firmware/rk3576_spl_loader.bin
sudo rkdeveloptool wl 0 firmware/rock-4d-spi-base.img
sudo rkdeveloptool rd
```

或者用 [RKDevelopToolGUI](https://github.com/gahingwoo/RKDevelopTool-GUI/) 使用GUI烧写

### 接下来做什么？

- 安装一个正经的操作系统（Ubuntu、Debian、Fedora ARM64）
- 测试 USB 启动
- 看看 NVMe 能不能用
- 帮忙改进 ACPI 表
- 报告 Bug

### 参与贡献

发现 Bug？在不同硬件同CPU上跑通了？想改进什么东西？

> **吾日三省吾身 For Bugs**  
> **1. 吾心所向，为何所期？**  
> **2. 代码所行，结果可得？**  
> **3. 理想现实，落差几何？**

开个 issue 或者发 pull request！我很乐意听到你的声音。

### 致谢

- **Rockchip** - RK3576 SoC
- **Radxa** - ROCK 4D 开发板
- **ARM** - Trusted Firmware
- **TianoCore** - EDK2 UEFI 实现
- **Linux 社区** - 设备树源码

### 许可证

这个编译脚本使用 MIT 许可证。固件组件有各自的许可证：
- ATF: BSD-3-Clause
- EDK2: BSD-2-Clause-Patent
- U-Boot: GPL-2.0

---

## Support / 支持

- GitHub Issues: Report bugs here / 在这里报告 Bug
- Forum: [Radxa Forum](https://forum.radxa.com/)
- Docs: [ROCK 4D Docs](https://docs.radxa.com/en/rock4/rock4d)

## Disclaimer / 免责声明

**English**: This is experimental firmware. Use at your own risk. We're not responsible if you brick your board (though it's pretty hard to do that permanently).

**中文**：这是实验性固件。使用风险自负。如果你搞砖了板子我们不负责（虽然软件搞到永久变砖挺难的）。

---

Made with ❤️ by the community / 社区为爱发电制作

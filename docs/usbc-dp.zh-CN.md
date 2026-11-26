# USB-C DisplayPort（实验性）

[English](usbc-dp.md) | 简体中文

分支 `usbc-dp`。泰山派 3M 或 CM5-IO 的 USB-C 口通过 DisplayPort Alt Mode 接显示器，在 Windows 下作为 HDMI 旁边的第二块屏。这里写固件这一半；Windows 显示驱动那一半见 wddm-mali-g52 的 `docs/USBC-DP.md`（分支 `usbc-dp`）。**还没有在板子上跑过。** 默认关闭：选项关着时启动流程没有任何变化。

## 用法

1. **开机前**把 USB-C 显示器、扩展坞或转接头插好。
2. 固件设置，RK3576 设置：**USB-C DisplayPort (experimental)** 设为 Enabled（只在泰山派 3M 和 CM5-IO 上出现）。
3. 在 Windows 里把显示驱动的 `UsbcDp` 设备参数设为 1。

不支持：开机后再插（Windows 下没有 Type-C 驱动在跑）；给这个口供电的显示器（它把自己报成电源端，这个口得当受电端并交换数据角色）；在 DP 上显示开机画面。

## 固件做了什么

在 ExitBootServices 时，UsbHcd 的处理函数里，Type-C 测量和 USBDP PHY 的 USB3 初始化之间：

1. `UsbTypeCGetOrientation`：FUSB302 照旧测 CC。一根线上有受电端就给 Rp；泰山派上它的 VBUS 开关打开。
2. **`UsbTypeCDpAltModeEnter`**（新增，板级库）：CM5-IO 上打开 VBUS（GPIO2_B6，DTS 里的 `vcc_5v0_typec0`）；按插入方向设置 SBU 偏置脚 GPIO4_C4/C5（正插 0/1，反插 1/0，和主线 `phy-rockchip-usbdp.c` 一样）；然后调用 `Rk3576Fusb302DpAltMode`：
   - 另一根 CC 线上是 Ra（带 e-marker 的线、靠 VCONN 供电的转接头）就打开 VCONN，判断方法同 `tcpm_src_attach`。
   - 以 5 V 电源端兼 DFP 身份走 PD 2.0：Source_Capabilities（一个 5 V 900 mA 的 PDO，支持 USB 通信）、Request、Accept、PS_RDY。发十次 caps 都没有 Request：把 VBUS 关一秒再开，只做一次（在开机菜单期间已经放弃 PD 的受电端，只有 VBUS 掉过之后才会重来）。
   - Discover Identity（要求支持模式）、Discover SVIDs（0xFF01）、Discover Modes、Enter Mode、DP Status Update、DP Configure，顺序和 Linux 的 tcpm 及 `altmodes/displayport.c` 一样。对方支持就用引脚分配 **D**（两条 lane 走 DP，另外两条保留 USB3），否则 **C**，再否则 E。
   - 如果 Status 的 ACK 里还没有 HPD，就最多等两秒，看 Attention 里会不会来（转接头后面的显示器可能起得晚）。
   - 途中对方发来的消息按 `tcpm.c` 在 SRC_READY 状态下的方式应答：Soft_Reset、Get_Source_Cap、新的 Request、角色交换（拒绝）、对方的 VDM 请求（NAK）。
3. USBDP PHY 的 USB3 初始化，在第 3 步放开 **DP_INIT_RSTN**，和主线在 USB+DP 模式下的做法一样；lane 复用还是一直用的那种（两个 pad 给 USB，两个给 DP）。
4. LCPLL 锁定后执行 `Rk3576UsbDpDpEnable`：打开时钟（aclk/hclk/pclk_vo1_root、clk_aux16mhz_0、aclk/pclk_dp0，以及电源域握手需要的 HDCP1 时钟）、设置 AUX 时钟分频、打开 **PD_VO1**（Vop2Rk3576.c 给 PD_VOP/PD_VO0 用的同一套步骤，换成 `rk3576_pm_domains` 里 VO1 的位）、释放 DP 复位、在 PMA 0x288 里打开两条 DP lane、写 VO1_GRF_SOC_CON0 的 lane 选择和 AUX 极性（反插时取反），并把控制器的 HPD 强制拉高。

串口日志每一步一行：`DP alt mode: <最后一步>; caps sent, VCONN, VBUS cycled; id, mode, status, config VDO; pin, HPD`，以及 `UsbDpPhy DP: PD_VO1 on, lanes 0x.., VO1_GRF_SOC_CON0 0x....`。

留给 Windows 的：开着自动 GoodCRC 的 FUSB302（对方之后发的消息都会被确认）、打开过的 VCONN、VBUS、PD_VO1 和 DP 时钟。链路速率、链路训练、EDID 和视频流由显示驱动来做（wddm-mali-g52 的 `dp.c`）。

## 为什么放在固件里，而不是 Windows 的 Type-C 驱动

Windows 对端口控制器有这几条路：UCSI 需要一个 PPM（跑 PD 的 EC 固件），这两块板子都没有；`UcmTcpciCx` 是微软的软件 PD 引擎，带 DisplayPort alt mode 通知，但客户端接口按 TCPCI 设计（FUSB302 不是 TCPCI，客户端要把 TCPCI 语义映射到它基于 FIFO token 的寄存器上）；UcmCx 客户端则要自带一整套 PD 协议栈。这几条都需要 Windows 的 I2C、GPIO 驱动和 FUSB302 的 ACPI 设备；泰山派上 FUSB302 所在的 I2C2 还和 HYM8563 共用，而固件的运行时 RTC 服务正在替 Windows 驱动 HYM8563。

对"开机前插好"这个目标，固件是最省事的地方：不要操作系统驱动，不要 ACPI 设备，运行时也没有总线共享问题，这份代码以后还能用来出开机画面。热插拔需要 Windows 驱动，那是第二阶段（见 wddm-mali-g52 的 `docs/USBC-DP.md`）。

## 为什么总是两条 DP lane

这个 PHY 的 USB3 部分本来就在 ExitBootServices 时按主线默认的 Type-C 映射初始化（正插 pad 0/1 给 USB、2/3 给 DP；反插反过来），之前的 WHEA 蓝屏就是一个背后没有在跑的 PHY 的 SS 端口引起的。保留这个映射、只在两个 DP pad 上加 DP，就不用复位 PHY，不用改 U3 端口，XHC0 也不会进入新状态。两条 lane 跑 HBR2 是 8.64 Gb/s，超过 VOP2 VP1 最大模式（2560x1600 60 Hz）的需要。主线在 C、D、E 三种引脚分配下 DP lane 0 和 1 用的是同一组 pad，所以只支持 C 的转接头用两条 lane 也能工作；接收端会在 DPCD 0x101 里看到 lane 数为 2。

## 代码

| 文件 | 内容 |
|---|---|
| `Silicon/Rockchip/RK3576/Library/Rk3576Fusb302Lib/Rk3576Fusb302Pd.c` | 基于 FUSB302 的 PD 和 alt mode |
| `.../Rk3576Fusb302Lib/UsbPdDp.h` | 消息的算术部分，在主机上测试（`scripts/tests/usbpd-host`，CI 任务 `host-tests`） |
| `.../Rk3576Fusb302Lib/Rk3576Fusb302Lib.c` | I2C 突发传输，375 kHz 下的总线打开/关闭 |
| `Silicon/Rockchip/Drivers/UsbHcdInitDxe/UsbHcd.c` | 调用入口、DP_INIT_RSTN、`Rk3576UsbDpDpEnable` |
| `Platform/*/Library/RockchipPlatformLib/RockchipPlatformLib.c` | 各板的 `UsbTypeCDpAltModeEnter`（ROCK 4D：不支持） |
| `RK3576.dec`、`RK3576Base.dsc.inc`、各板 `.dsc`、`RK3576Dxe` | 选项：`PcdUsbcDp`（变量 `UsbcDp`）、`PcdUsbcDpSupported` |

规模：PD 和 alt mode 连头文件约 760 行，PHY/时钟/电源部分约 150 行，选项和板级约 200 行。

## 板上测试（泰山派 3M）

1. 从 PR 的 `build` 运行里下载 artifact `tspi3m-firmware`，取 `TaishanPi3M-emmc.img`。只写固件那一段，Windows 和 UEFI 设置都保留（docs/flash.md 的"更新固件并保留其他内容"）：
   `dd if=TaishanPi3M-emmc.img of=firmware.bin bs=512 skip=64 count=43584`，然后 `rkdeveloptool db rk3576_spl_loader.bin`、`rkdeveloptool wl 64 firmware.bin`、`rkdeveloptool rd`。
2. 选项关（默认）：USB-C 上接 USB 硬盘启动一次 Windows，必须和以前一样（USB-C 仍是 USB 3，日志没有新行）。
3. 把 DP 显示器或转接头插到 USB-C，开机进设置：**USB-C DisplayPort (experimental)** = Enabled，保存、重启。
4. 操作系统启动时看串口：`DP alt mode:` 那行（要有 `config` 和 `HPD`）以及 `UsbDpPhy DP: PD_VO1 on` 那行。驱动的 `UsbcDp` 仍为 0 时，Windows 必须照常在 HDMI 上进桌面。
5. 再装 wddm-mali-g52 的 `usbc-dp`，把 `UsbcDp` 设为 1（见它的 `docs/USBC-DP.md`）。两个插入方向各启动三次。

回退：设置里改回 Disabled，或刷回 main 的固件。

## 风险

- 在 375 kHz I2C 上轮询做 PD 时序：每次查 FIFO 都是一次寄存器读；预计几十毫秒内就有应答，等待时间给了 60 ms。
- 开机之后 FUSB302 的 RX FIFO 不再被读取。如果对方发的消息多到把它塞满，芯片会怎样，在读过的资料里没有写；对方可能因此复位 PD，掉出 alt mode。
- 只有在选项打开、而且受电端始终不发 Request 时才会重启 VBUS；这时口上不支持 PD 的 USB 硬盘会在 ExitBootServices 时断电重来一次（在 Windows 的 USB 协议栈启动之前）。
- CM5-IO 上 VBUS 在 GPIO2_B6、FUSB302 的 VCONN 供电，来自 DTS 和原理图文字，没有实测。

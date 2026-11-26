# USB-C DisplayPort (experimental)

[English](usbc-dp.md) | [简体中文](usbc-dp.zh-CN.md)

On `main`, off by default. A monitor on the USB-C port of a TaishanPi 3M or a CM5-IO
through DisplayPort Alt Mode, as a second display next to HDMI under
Windows. The firmware's half is described here; the Windows display driver's
half is wddm-mali-g52's [USBC-DP.md](https://github.com/gahingwoo/wddm-mali-g52/blob/main/docs/USBC-DP.md). **Not run on a
board yet.** With the option off nothing in the boot changes.

## Using it

1. Plug the USB-C monitor, dock or adapter in **before power-on**.
2. Setup, RK3576 settings: **USB-C DisplayPort (experimental)** = Enabled
   (only on the TaishanPi 3M and the CM5-IO).
3. In Windows, the display driver's `UsbcDp` Device Parameter = 1.

Not supported: plugging in after boot (no Type-C driver runs under Windows),
a monitor that powers this port (it presents itself as a source; the port
would need to be a sink and swap data roles), the boot screen on DP.

## What the firmware does

At ExitBootServices, in UsbHcd's handler, between the Type-C measurement and
the USBDP PHY's USB3 bring-up:

1. `UsbTypeCGetOrientation`: the FUSB302 measures CC, as before. A sink on
   one line gets Rp; on the TaishanPi its VBUS switch goes on.
2. **`UsbTypeCDpAltModeEnter`** (new, board library): on the CM5-IO VBUS on
   (GPIO2_B6, `vcc_5v0_typec0` in the DTS); the SBU bias pins GPIO4_C4/C5 by
   orientation (normal 0/1, flipped 1/0, as mainline's
   `phy-rockchip-usbdp.c`); then `Rk3576Fusb302DpAltMode`:
   - VCONN on the other CC line if it shows Ra (an e-marked cable, a
     VCONN-powered adapter), as `tcpm_src_attach` decides.
   - PD 2.0 as a 5 V source and DFP: Source_Capabilities (one 5 V 900 mA
     PDO, USB communications capable), Request, Accept, PS_RDY. No Request
     within ten caps: VBUS off for one second and on again, once (a sink
     that gave up on PD during the boot menu only starts over after VBUS
     goes away).
   - Discover Identity (modal), Discover SVIDs (0xFF01), Discover Modes,
     Enter Mode, DP Status Update, DP Configure, the order Linux's tcpm and
     `altmodes/displayport.c` use. Pin assignment **D** when the partner has
     it (DP on two lanes, USB3 kept on the other two), else **C**, else E.
   - Waits up to two seconds for HPD in an Attention if the Status ACK did
     not have it (an adapter whose own monitor comes up later).
   - Partner messages on the way are answered as `tcpm.c` answers them in
     SRC_READY: Soft_Reset, Get_Source_Cap, a new Request, role swaps
     (rejected), the partner's VDM requests (NAK).
3. The USBDP PHY's USB3 bring-up with **DP_INIT_RSTN** released at step 3,
   as mainline does for USB+DP mode; the lane mux is the one it always used
   (two pads USB, two DP).
4. After the LCPLL locks, `Rk3576UsbDpDpEnable`: clocks (aclk/hclk/pclk_vo1_root,
   clk_aux16mhz_0, aclk/pclk_dp0, the HDCP1 clocks the power domain's
   handshake needs), the AUX clock divider, **PD_VO1** on (the sequence
   Vop2Rk3576.c runs for PD_VOP/PD_VO0, with `rk3576_pm_domains`' VO1
   bits), the DP resets released, the two DP lane enables in PMA 0x288,
   VO1_GRF_SOC_CON0 lane select and AUX polarity (inverted when flipped),
   and the controller's HPD forced high.

The serial log has one line per step: `DP alt mode: <last step>; caps sent,
VCONN, VBUS cycled; id, mode, status, config VDOs; pin, HPD` and
`UsbDpPhy DP: PD_VO1 on, lanes 0x.., VO1_GRF_SOC_CON0 0x....`.

What is left running for Windows: the FUSB302 with automatic GoodCRC (so the
partner's later messages are acknowledged), VCONN if it was turned on, VBUS,
PD_VO1 and the DP clocks. The display driver does the link rate, training,
EDID and the video stream (wddm-mali-g52 `dp.c`).

## Why the firmware, and not a Windows Type-C driver

Windows' options for a port controller: UCSI needs a PPM (an EC firmware
running PD), which these boards do not have; `UcmTcpciCx` is Microsoft's
software PD engine with DisplayPort alt mode notifications, over a
TCPCI-shaped client interface (the FUSB302 is not TCPCI; the client would
map onto its FIFO-token register set); a UcmCx client would carry its own PD
stack. Each needs the Windows I2C and GPIO drivers and an ACPI device for
the FUSB302, and on the TaishanPi the FUSB302's I2C2 is shared with the
HYM8563 that the firmware's runtime RTC service drives for Windows.

For "plugged in before boot", the firmware is the cheapest place: no OS
driver, no ACPI device, no bus sharing at runtime, and the same code can
later give a boot picture. Hot-plug needs the Windows driver; that is phase
2 (wddm-mali-g52's [USBC-DP.md](https://github.com/gahingwoo/wddm-mali-g52/blob/main/docs/USBC-DP.md)).

## Why always two DP lanes

The USB3 half of this PHY is already brought up at ExitBootServices with the
mainline default Type-C mapping (pads 0/1 USB, 2/3 DP for a normal plug;
the other way round flipped), and the earlier WHEA bugcheck came from an SS
port with no running PHY behind it. Keeping that mapping and adding DP on
the two DP pads means no PHY reset, no change to the U3 port and no new
state for XHC0. Two lanes at HBR2 carry 8.64 Gb/s, more than VOP2 VP1's
largest mode (2560x1600 at 60 Hz) needs. Mainline uses the same pad for DP
lanes 0 and 1 in pin assignments C, D and E, so a C-only adapter works on two
lanes as well; the sink sees lane count 2 in DPCD 0x101.

## Code

| File | What |
|---|---|
| `Silicon/Rockchip/RK3576/Library/Rk3576Fusb302Lib/Rk3576Fusb302Pd.c` | PD and alt mode over the FUSB302 |
| `.../Rk3576Fusb302Lib/UsbPdDp.h` | message arithmetic, host-tested (`scripts/tests/usbpd-host`, CI job `host-tests`) |
| `.../Rk3576Fusb302Lib/Rk3576Fusb302Lib.c` | I2C burst transfers, bus open/close at 375 kHz |
| `Silicon/Rockchip/Drivers/UsbHcdInitDxe/UsbHcd.c` | the call, DP_INIT_RSTN, `Rk3576UsbDpDpEnable` |
| `Platform/*/Library/RockchipPlatformLib/RockchipPlatformLib.c` | `UsbTypeCDpAltModeEnter` per board (ROCK 4D: unsupported) |
| `RK3576.dec`, `RK3576Base.dsc.inc`, boards' `.dsc`, `RK3576Dxe` | the option: `PcdUsbcDp` (variable `UsbcDp`), `PcdUsbcDpSupported` |

Sizes: PD and alt mode about 760 lines with the header, the PHY/clock/power
part about 150, option and boards about 200.

## Board test (TaishanPi 3M)

1. From the PR's `build` run, artifact `tspi3m-firmware`, take
   `TaishanPi3M-emmc.img`. Flash only the firmware range so Windows and
   the UEFI settings stay
   ([flashing](flash.md#updating-the-firmware-and-keeping-everything-else)).
2. Option off (the default): boot Windows once with a USB disk on USB-C.
   It must behave as before (USB-C still USB 3, no new log lines).
3. Plug the DP monitor or adapter into USB-C, power on, setup:
   **USB-C DisplayPort (experimental)** = Enabled, save, reset.
4. Serial log at OS start: the `DP alt mode:` line (want `config` and
   `HPD`) and the `UsbDpPhy DP: PD_VO1 on` line. Windows must still
   reach the desktop on HDMI with the driver's `UsbcDp` still 0.
5. Then install wddm-mali-g52 `usbc-dp` and set `UsbcDp` = 1 ([USBC-DP.md](https://github.com/gahingwoo/wddm-mali-g52/blob/main/docs/USBC-DP.md)). Both plug orientations, three boots each.

Back out: setup option to Disabled, or reflash the main build.

## Risks

- PD timing in a polled loop at 375 kHz I2C: each FIFO poll is a register
  read; answers within tens of milliseconds are expected, the waits allow
  60 ms.
- The FUSB302's RX FIFO is never drained after boot. If a partner sends
  enough messages to fill it, what the chip does is not documented in what
  was read; the partner may then reset PD and drop the alt mode.
- VBUS cycling only happens when the option is on and a sink never sent a
  Request; a USB disk without PD on the port is power-cycled once at
  ExitBootServices then (before Windows' USB stack starts).
- CM5-IO VBUS on GPIO2_B6 and the FUSB302's VCONN supply are from the DTS
  and schematic text, not measured.

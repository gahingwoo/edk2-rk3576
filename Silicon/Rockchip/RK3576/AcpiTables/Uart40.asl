/** @file
 *
 *  [SSDT body] One RK3576 UART for an application: a DesignWare APB UART
 *  with 32-bit registers four bytes apart, for the woa-rk3576 rk3xuart
 *  SerCx2 driver (ACPI\RKCP3006).
 *
 *  Included by a board's 40-pin UART SSDT (OEM table ID "RKUART40"), once
 *  per UART, inside Scope (\_SB), with these defined first:
 *
 *    RK_UART40_NAME      the device name, e.g. URT4
 *    RK_UART40_UID       the controller number n of UARTn
 *    RK_UART40_BASE      its registers (rk3576.dtsi serial@...)
 *    RK_UART40_GSIV      its interrupt, GIC SPI + 32
 *    RK_UART40_PORTNAME  the port name Windows publishes, e.g. "UART4"
 *
 *  RK3576AcpiPlatformDxe installs that SSDT only when "40-pin UART" is on in
 *  setup, and RK3576Dxe then sets the UART's pins and sclk_uartN = xin24m
 *  (Pin40.c), which is the "clock-frequency" below.
 *
 *  "SerCx-FriendlyName" makes SerCx2 publish a GUID_DEVINTERFACE_COMPORT
 *  interface for the UART with that port name (Windows 10 1903 and later,
 *  "Device Interface Publication for a SerCx or SerCx2 Managed Serial
 *  Port", Microsoft Learn), which Windows.Devices.SerialCommunication and
 *  Win32 programs open.  Such a port must not also be a UARTSerialBusV2
 *  connection of any other device, so the RhProxy SSDT does not list it.
 *
 *  The _CID and "compatible" let an ACPI Linux bind 8250_dw (PRP0001), as
 *  the other RK3576 devices here do.
 *
 *  UART0 is the firmware console and the debugger's port (SPCR, DBG2):
 *  never instantiate this for it.  rk3xuart refuses it as well.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

Device (RK_UART40_NAME) {
  Name (_HID, "RKCP3006")
  Name (_CID, "PRP0001")
  Name (_UID, RK_UART40_UID)
  Name (_CCA, 0)

  Name (_CRS, ResourceTemplate () {
    Memory32Fixed (ReadWrite, RK_UART40_BASE, 0x1000)
    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { RK_UART40_GSIV }
  })

  Name (_DSD, Package () {
    ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package () { "SerCx-FriendlyName", RK_UART40_PORTNAME },
      Package () { "compatible", Package () { "rockchip,rk3576-uart", "snps,dw-apb-uart" } },
      Package () { "reg-shift", 2 },
      Package () { "reg-io-width", 4 },
      Package () { "clock-frequency", 24000000 },
    }
  })

  Method (_STA) {
    Return (0xF)
  }
}

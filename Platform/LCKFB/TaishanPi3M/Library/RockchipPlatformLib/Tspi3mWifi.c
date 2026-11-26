/** @file
 *
 *  Tspi3mWifi.c: bring up the TaishanPi 3M's SDIO Wi-Fi for an operating
 *  system that has no pin, clock or power-sequence driver of its own
 *  (Windows with the woa-rk3576 drivers, docs/WIFI.md there), and probe the
 *  card once so the serial log says whether it answers.
 *
 *  Runs only when the "SDIO Wi-Fi" setup option is on (PcdAcpiSdioWifi,
 *  off by default), from RK3576Dxe.  With the option off nothing here runs
 *  and the module stays off, as before.
 *
 *  The module is an Ampak AP6256 (BCM43456 Wi-Fi, BCM4345C5 Bluetooth).
 *  From the board's mainline device tree (rk3576-lckfb-tspi-3m.dts: &sdio,
 *  sdio_pwrseq, vcc_3v3_wl, wifi_* pinctrl) and rk3576.dtsi (sdio:
 *  mmc@2a320000):
 *
 *    SDIO      sdmmc1m0: D0..D3 GPIO1 PB4..PB7, CMD GPIO1 PC0, CLK GPIO1 PC1,
 *              function 2, pull-up (drive level 2 in the tree; this GpioLib
 *              cannot set drive strength on RK3576, so the reset default
 *              stays, which is enough for the 400 kHz probe below).
 *    WL_REG_ON GPIO1 PC6, active high (the tree's reset-gpios is the same
 *              pin, active low), pull-up in the tree's pinctrl.
 *    HOST_WAKE GPIO0 PB0, module to host, level high, pull-down.  Set up as
 *              an input here; nothing uses it yet.
 *    VBAT      always on: Q9, the switch WIFI_PWREN_L (GPIO0 PD1) would
 *              drive, is not fitted (see Tspi3mBluetooth.c).
 *    32 kHz    the HYM8563 CLKOUT, kept on by Hym8563RealTimeClockLib.
 *
 *  CRU (0x27200000), from mainline clk-rk3576.c and rst-rk3576.c:
 *    CLKSEL_CON104  0x4A0  [7:6] cclk_src_sdio mux (0 GPLL, 1 CPLL,
 *                          2 xin24m), [5:0] divider - 1
 *    GATE_CON42     0x8A8  bit 11 cclk_src_sdio, bit 12 hclk_sdio
 *    SOFTRST_CON42  0xAA8  bit 12 SRST_H_SDIO
 *
 *  The controller is a DesignWare MSHC (dw_mmc), not SDHCI, the same IP as
 *  the SD slot at 0x2a310000.  Mainline dw_mmc-rockchip.c: the controller
 *  sees the CRU rate divided by two (RK3288_CLKGEN_DIV), and the card clock
 *  is that divided by 2 * CLKDIV.  24 MHz from xin24m and CLKDIV 15 give
 *  400 kHz.
 *
 *  The probe, at 400 kHz on a 1-bit bus, by polling (no interrupt is ever
 *  enabled):
 *    CMD0 with the 80 initialisation clocks, CMD5 for the OCR, CMD5 again
 *    until the card is ready, CMD3 for the RCA, CMD7 to select it, CMD52
 *    reads of the CCCR and the common CIS (CISTPL_MANFID gives the vendor
 *    and device IDs; 0x02d0:0xa9bf is expected for a BCM4345x), then
 *    function 1 enabled, the ALP clock requested through ChipClockCSR, and
 *    the chip ID read from 0x18000000 through the backplane window with a
 *    4-byte CMD53, as brcmfmac's brcmf_sdio_probe_attach does.  0x4345 rev
 *    9 is a BCM43456.
 *  Then WL_REG_ON is dropped and raised again so the OS finds the chip as it
 *  is after power-on, and the controller is left with its clock off and all
 *  interrupts masked and acknowledged.
 *
 *  Every step logs one line starting "TSPI-3M Wi-Fi:"; the last one says
 *  "probe PASS" or "probe FAIL at <step>".
 *
 *  NOT RUN ON HARDWARE.  It compiles.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 **/

#include <Base.h>
#include <Uefi/UefiBaseType.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/GpioLib.h>
#include <Library/TimerLib.h>
#include <Soc.h>

#define WIFI_LOG  "TSPI-3M Wi-Fi: "

//
// CRU
//
#define SDIO_CLKSEL             CRU_CLKSEL_CON (104)
#define SDIO_CLKSEL_MUX_SHIFT   6
#define SDIO_CLKSEL_MUX_MASK    0x3U
#define SDIO_CLKSEL_DIV_MASK    0x3FU
#define SDIO_CLKSEL_XIN24M      2
#define SDIO_GATE               CRU_CLKGATE_CON (42)
#define SDIO_GATE_CCLK          BIT11
#define SDIO_GATE_HCLK          BIT12
#define SDIO_SOFTRST            CRU_SOFTRST_CON (42)
#define SDIO_SOFTRST_HCLK       BIT12

//
// dw_mmc registers (mainline drivers/mmc/host/dw_mmc.h)
//
#define SDIO_BASE        0x2A320000UL
#define MSHC_CTRL        0x000
#define MSHC_PWREN       0x004
#define MSHC_CLKDIV      0x008
#define MSHC_CLKSRC      0x00C
#define MSHC_CLKENA      0x010
#define MSHC_TMOUT       0x014
#define MSHC_CTYPE       0x018
#define MSHC_BLKSIZ      0x01C
#define MSHC_BYTCNT      0x020
#define MSHC_INTMASK     0x024
#define MSHC_CMDARG      0x028
#define MSHC_CMD         0x02C
#define MSHC_RESP0       0x030
#define MSHC_RINTSTS     0x044
#define MSHC_STATUS      0x048
#define MSHC_VERID       0x06C
#define MSHC_BMOD        0x080
#define MSHC_IDSTS       0x08C
#define MSHC_IDINTEN     0x090
#define MSHC_FIFO        0x200    // DATA_240A_OFFSET; VERID is 0x270a here

#define CTRL_RESET       BIT0
#define CTRL_FIFO_RESET  BIT1
#define CTRL_DMA_RESET   BIT2
#define CTRL_INT_ENABLE  BIT4
#define CTRL_USE_IDMAC   BIT25

#define CMD_START        BIT31
#define CMD_UPD_CLK      BIT21
#define CMD_INIT         BIT15
#define CMD_PRV_DAT_WAIT BIT13
#define CMD_DAT_EXP      BIT9
#define CMD_RESP_CRC     BIT8
#define CMD_RESP_EXP     BIT6

#define INT_EBE          BIT15
#define INT_SBE          BIT13
#define INT_HLE          BIT12
#define INT_FRUN         BIT11
#define INT_HTO          BIT10
#define INT_DRTO         BIT9
#define INT_RTO          BIT8
#define INT_DCRC         BIT7
#define INT_RCRC         BIT6
#define INT_DTO          BIT3
#define INT_CMD_DONE     BIT2
#define INT_RE           BIT1
#define INT_CMD_ERRORS   (INT_RTO | INT_RCRC | INT_RE | INT_HLE)
#define INT_DATA_ERRORS  (INT_DRTO | INT_DCRC | INT_EBE | INT_SBE | INT_FRUN | INT_HTO)

#define STATUS_FCNT(x)   (((x) >> 17) & 0x1FFF)
#define STATUS_BUSY      BIT9

//
// SDIO (SDIO Simplified Specification 3.00; Linux include/linux/mmc/sdio.h)
//
#define SDIO_CCCR_REV        0x00
#define SDIO_CCCR_IOEX       0x02
#define SDIO_CCCR_IORX       0x03
#define SDIO_CCCR_CAPS       0x08
#define SDIO_CCCR_CIS        0x09
#define CISTPL_NULL          0x00
#define CISTPL_MANFID        0x20
#define CISTPL_END           0xFF

#define R4_READY             BIT31
#define R4_NUM_FUNCS(r)      (((r) >> 28) & 0x7)
#define R4_MEM_PRESENT       BIT27
#define R4_OCR_MASK          0x00FFFFFFU
#define OCR_32_34            (BIT20 | BIT21)

#define R5_ERROR_BITS        0xCB00U   // COM_CRC, ILLEGAL, ERROR, FUNC_NUM, OUT_OF_RANGE

//
// Broadcom SDIO function 1 (brcmfmac sdio.h, sdio.c, chipcommon.h, soc.h)
//
#define SBSDIO_FUNC1_SBADDRLOW    0x1000A
#define SBSDIO_FUNC1_CHIPCLKCSR   0x1000E
#define SBSDIO_FORCE_ALP          0x01
#define SBSDIO_ALP_AVAIL_REQ      0x08
#define SBSDIO_FORCE_HW_CLKREQ_OFF 0x20
#define SBSDIO_ALP_AVAIL          0x40
#define SBSDIO_SB_OFT_ADDR_MASK   0x07FFF
#define SBSDIO_SB_ACCESS_2_4B_FLAG 0x08000
#define SI_ENUM_BASE              0x18000000U

#define BCM_SDIO_VENDOR           0x02D0
#define BCM43455_SDIO_DEVICE      0xA9BF
#define BCM4345_CHIP_ID           0x4345

//
// Pins
//
#define WL_REG_ON_GROUP  1
#define WL_REG_ON_PIN    GPIO_PIN_PC6
#define HOST_WAKE_GROUP  0
#define HOST_WAKE_PIN    GPIO_PIN_PB0

STATIC
VOID
WifiPinsAndClocks (
  VOID
  )
{
  UINT8  Pin;

  /* cclk_src_sdio: 24 MHz from xin24m. */
  MmioWrite32 (
    SDIO_CLKSEL,
    (((SDIO_CLKSEL_MUX_MASK << SDIO_CLKSEL_MUX_SHIFT) | SDIO_CLKSEL_DIV_MASK) << 16) |
    (SDIO_CLKSEL_XIN24M << SDIO_CLKSEL_MUX_SHIFT) | 0
    );

  /* Both SDIO clocks ungated, then the AHB reset pulsed. */
  MmioWrite32 (SDIO_GATE, ((SDIO_GATE_CCLK | SDIO_GATE_HCLK) << 16) | 0);
  MmioWrite32 (SDIO_SOFTRST, (SDIO_SOFTRST_HCLK << 16) | SDIO_SOFTRST_HCLK);
  MicroSecondDelay (10);
  MmioWrite32 (SDIO_SOFTRST, (SDIO_SOFTRST_HCLK << 16) | 0);
  MicroSecondDelay (10);

  /* sdmmc1m0: D0..D3 = GPIO1 PB4..PB7, CMD = PC0, CLK = PC1, function 2. */
  for (Pin = GPIO_PIN_PB4; Pin <= GPIO_PIN_PB7; Pin++) {
    GpioPinSetFunction (1, Pin, 2);
    GpioPinSetPull (1, Pin, GPIO_PIN_PULL_UP);
  }

  GpioPinSetFunction (1, GPIO_PIN_PC0, 2);
  GpioPinSetPull (1, GPIO_PIN_PC0, GPIO_PIN_PULL_UP);
  GpioPinSetFunction (1, GPIO_PIN_PC1, 2);
  GpioPinSetPull (1, GPIO_PIN_PC1, GPIO_PIN_PULL_UP);

  /* WL_REG_ON as an output, low for now; HOST_WAKE an input, pulled down. */
  GpioPinSetFunction (WL_REG_ON_GROUP, WL_REG_ON_PIN, 0);
  GpioPinSetPull (WL_REG_ON_GROUP, WL_REG_ON_PIN, GPIO_PIN_PULL_NONE);
  GpioPinWrite (WL_REG_ON_GROUP, WL_REG_ON_PIN, FALSE);
  GpioPinSetDirection (WL_REG_ON_GROUP, WL_REG_ON_PIN, GPIO_PIN_OUTPUT);

  GpioPinSetFunction (HOST_WAKE_GROUP, HOST_WAKE_PIN, 0);
  GpioPinSetPull (HOST_WAKE_GROUP, HOST_WAKE_PIN, GPIO_PIN_PULL_DOWN);
  GpioPinSetDirection (HOST_WAKE_GROUP, HOST_WAKE_PIN, GPIO_PIN_INPUT);

  DEBUG ((
    DEBUG_INFO,
    WIFI_LOG "pins sdmmc1m0 fn2, cclk_src_sdio 24 MHz (CLKSEL104 0x%08x, GATE42 0x%08x)\n",
    MmioRead32 (SDIO_CLKSEL),
    MmioRead32 (SDIO_GATE)
    ));
}

STATIC
VOID
WifiRegOn (
  IN BOOLEAN  On,
  IN UINTN    SettleMs
  )
{
  GpioPinWrite (WL_REG_ON_GROUP, WL_REG_ON_PIN, On);
  MicroSecondDelay (SettleMs * 1000);
}

STATIC
UINT32
Rd (
  IN UINTN  Offset
  )
{
  return MmioRead32 (SDIO_BASE + Offset);
}

STATIC
VOID
Wr (
  IN UINTN   Offset,
  IN UINT32  Value
  )
{
  MmioWrite32 (SDIO_BASE + Offset, Value);
}

STATIC
EFI_STATUS
MshcWaitCmdTaken (
  VOID
  )
{
  UINTN  Us;

  for (Us = 0; Us < 100000; Us += 10) {
    if ((Rd (MSHC_CMD) & CMD_START) == 0) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (10);
  }

  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
MshcUpdateClock (
  VOID
  )
{
  Wr (MSHC_CMD, CMD_START | CMD_UPD_CLK | CMD_PRV_DAT_WAIT);
  return MshcWaitCmdTaken ();
}

STATIC
EFI_STATUS
MshcResetAndClock (
  VOID
  )
{
  UINTN       Us;
  EFI_STATUS  Status;

  Wr (MSHC_PWREN, 1);
  Wr (MSHC_CTRL, CTRL_RESET | CTRL_FIFO_RESET | CTRL_DMA_RESET);
  for (Us = 0; Us < 10000; Us += 10) {
    if ((Rd (MSHC_CTRL) & (CTRL_RESET | CTRL_FIFO_RESET | CTRL_DMA_RESET)) == 0) {
      break;
    }

    MicroSecondDelay (10);
  }

  if (Us >= 10000) {
    return EFI_DEVICE_ERROR;
  }

  Wr (MSHC_BMOD, 0);
  Wr (MSHC_IDINTEN, 0);
  Wr (MSHC_IDSTS, 0xFFFFFFFF);
  Wr (MSHC_INTMASK, 0);
  Wr (MSHC_RINTSTS, 0xFFFFFFFF);
  Wr (MSHC_TMOUT, 0xFFFFFFFF);
  Wr (MSHC_CTYPE, 0);         // 1-bit
  Wr (MSHC_BLKSIZ, 512);

  Wr (MSHC_CLKENA, 0);
  Status = MshcUpdateClock ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Wr (MSHC_CLKSRC, 0);
  Wr (MSHC_CLKDIV, 15);       // 24 MHz / 2 (CLKGEN) / (2 * 15) = 400 kHz
  Status = MshcUpdateClock ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Wr (MSHC_CLKENA, 1);        // card clock on, no low-power gating
  return MshcUpdateClock ();
}

//
// Send one command and wait for it (and, with DataBytes, for its data).
// Returns EFI_NO_RESPONSE on a response timeout, EFI_CRC_ERROR on a response
// CRC error, EFI_DEVICE_ERROR otherwise.
//
STATIC
EFI_STATUS
MshcCommand (
  IN  UINT8   Index,
  IN  UINT32  Argument,
  IN  UINT32  Flags,
  IN  UINT32  DataBytes,
  OUT UINT32  *Response,
  OUT UINT32  *Data       OPTIONAL
  )
{
  UINT32      Rint;
  UINTN       Us;
  EFI_STATUS  Status;

  Rint = 0;
  Wr (MSHC_RINTSTS, 0xFFFFFFFF);

  if (DataBytes != 0) {
    Wr (MSHC_CTRL, Rd (MSHC_CTRL) | CTRL_FIFO_RESET);
    for (Us = 0; Us < 1000 && (Rd (MSHC_CTRL) & CTRL_FIFO_RESET); Us += 10) {
      MicroSecondDelay (10);
    }

    Wr (MSHC_BLKSIZ, DataBytes);
    Wr (MSHC_BYTCNT, DataBytes);
    Flags |= CMD_DAT_EXP;
  }

  Wr (MSHC_CMDARG, Argument);
  Wr (MSHC_CMD, CMD_START | CMD_PRV_DAT_WAIT | Flags | (Index & 0x3F));

  Status = MshcWaitCmdTaken ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Us = 0; Us < 100000; Us += 10) {
    Rint = Rd (MSHC_RINTSTS);
    if (Rint & INT_CMD_DONE) {
      break;
    }

    MicroSecondDelay (10);
  }

  if ((Rint & INT_CMD_DONE) == 0) {
    return EFI_TIMEOUT;
  }

  if (Rint & INT_RTO) {
    return EFI_NO_RESPONSE;
  }

  if ((Rint & INT_RCRC) && (Flags & CMD_RESP_CRC)) {
    return EFI_CRC_ERROR;
  }

  if (Rint & (INT_RE | INT_HLE)) {
    return EFI_DEVICE_ERROR;
  }

  *Response = Rd (MSHC_RESP0);

  if (DataBytes != 0) {
    for (Us = 0; Us < 100000; Us += 10) {
      Rint = Rd (MSHC_RINTSTS);
      if (Rint & (INT_DTO | INT_DATA_ERRORS)) {
        break;
      }

      MicroSecondDelay (10);
    }

    if ((Rint & INT_DTO) == 0 || (Rint & (INT_DATA_ERRORS & ~INT_HTO))) {
      DEBUG ((DEBUG_INFO, WIFI_LOG "CMD%u data RINTSTS 0x%08x STATUS 0x%08x\n", Index, Rint, Rd (MSHC_STATUS)));
      return EFI_DEVICE_ERROR;
    }

    if ((Data != NULL) && (STATUS_FCNT (Rd (MSHC_STATUS)) >= 1)) {
      *Data = Rd (MSHC_FIFO);
    } else {
      return EFI_DEVICE_ERROR;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Cmd52 (
  IN  BOOLEAN  Write,
  IN  UINT8    Function,
  IN  UINT32   Address,
  IN  UINT8    In,
  OUT UINT8    *Out
  )
{
  UINT32      Arg;
  UINT32      R5;
  EFI_STATUS  Status;

  Arg = ((Write ? 1U : 0U) << 31) | ((UINT32)(Function & 7) << 28) |
        (Write ? BIT27 : 0) |               // RAW: read back what was written
        ((Address & 0x1FFFF) << 9) | In;

  Status = MshcCommand (52, Arg, CMD_RESP_EXP | CMD_RESP_CRC, 0, &R5, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (R5 & R5_ERROR_BITS) {
    DEBUG ((DEBUG_INFO, WIFI_LOG "CMD52 fn%u 0x%05x R5 flags 0x%04x\n", Function, Address, R5 & 0xFF00));
    return EFI_DEVICE_ERROR;
  }

  if (Out != NULL) {
    *Out = (UINT8)(R5 & 0xFF);
  }

  return EFI_SUCCESS;
}

//
// Walk the common CIS for CISTPL_MANFID.
//
STATIC
EFI_STATUS
ReadManfid (
  IN  UINT32  CisPtr,
  OUT UINT16  *Vendor,
  OUT UINT16  *Device
  )
{
  UINT32      Ptr;
  UINT8       Code;
  UINT8       Link;
  UINT8       B[4];
  UINTN       Tuples;
  UINTN       Index;
  EFI_STATUS  Status;

  Ptr = CisPtr;
  for (Tuples = 0; Tuples < 64; Tuples++) {
    Status = Cmd52 (FALSE, 0, Ptr, 0, &Code);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (Code == CISTPL_END) {
      return EFI_NOT_FOUND;
    }

    if (Code == CISTPL_NULL) {
      Ptr++;
      continue;
    }

    Status = Cmd52 (FALSE, 0, Ptr + 1, 0, &Link);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if ((Code == CISTPL_MANFID) && (Link >= 4)) {
      for (Index = 0; Index < 4; Index++) {
        Status = Cmd52 (FALSE, 0, Ptr + 2 + (UINT32)Index, 0, &B[Index]);
        if (EFI_ERROR (Status)) {
          return Status;
        }
      }

      *Vendor = (UINT16)(B[0] | (B[1] << 8));
      *Device = (UINT16)(B[2] | (B[3] << 8));
      return EFI_SUCCESS;
    }

    if (Link == 0xFF) {
      return EFI_NOT_FOUND;
    }

    Ptr += 2 + Link;
  }

  return EFI_NOT_FOUND;
}

//
// brcmf_sdiod_readl: set the backplane window, then a 4-byte CMD53 from
// function 1 with the 32-bit access flag.
//
STATIC
EFI_STATUS
BackplaneRead32 (
  IN  UINT32  Address,
  OUT UINT32  *Value
  )
{
  UINT32      Window;
  UINT32      Off;
  UINT32      Arg;
  UINT32      R5;
  UINTN       Index;
  EFI_STATUS  Status;

  Window = (Address & ~(UINT32)SBSDIO_SB_OFT_ADDR_MASK) >> 8;
  for (Index = 0; Index < 3; Index++, Window >>= 8) {
    Status = Cmd52 (TRUE, 1, SBSDIO_FUNC1_SBADDRLOW + (UINT32)Index, (UINT8)(Window & 0xFF), NULL);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Off = (Address & SBSDIO_SB_OFT_ADDR_MASK) | SBSDIO_SB_ACCESS_2_4B_FLAG;

  // CMD53 read, function 1, byte mode, incrementing address, 4 bytes.
  Arg = (1U << 28) | BIT26 | ((Off & 0x1FFFF) << 9) | 4;
  Status = MshcCommand (53, Arg, CMD_RESP_EXP | CMD_RESP_CRC, 4, &R5, Value);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return (R5 & R5_ERROR_BITS) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC
VOID
MshcQuiesce (
  VOID
  )
{
  Wr (MSHC_CTRL, Rd (MSHC_CTRL) & ~(CTRL_INT_ENABLE | CTRL_USE_IDMAC));
  Wr (MSHC_INTMASK, 0);
  Wr (MSHC_IDINTEN, 0);
  Wr (MSHC_BMOD, 0);
  Wr (MSHC_CLKENA, 0);
  (VOID)MshcUpdateClock ();
  Wr (MSHC_RINTSTS, 0xFFFFFFFF);
  Wr (MSHC_IDSTS, 0xFFFFFFFF);
}

STATIC
EFI_STATUS
WifiProbe (
  OUT CONST CHAR8  **FailedStep
  )
{
  EFI_STATUS  Status;
  UINT32      R;
  UINT32      Ocr;
  UINT32      Rca;
  UINT32      ChipId;
  UINTN       Try;
  UINT8       Rev;
  UINT8       Caps;
  UINT8       B;
  UINT8       Csr;
  UINT32      Cis;
  UINT16      Vendor;
  UINT16      Device;

  R    = 0;
  Rev  = 0;
  Caps = 0;
  B    = 0;
  Vendor = 0;
  Device = 0;
  ChipId = 0;

  *FailedStep = "controller reset";
  DEBUG ((DEBUG_INFO, WIFI_LOG "dw_mmc @0x%lx VERID 0x%08x\n", SDIO_BASE, Rd (MSHC_VERID)));
  Status = MshcResetAndClock ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *FailedStep = "CMD0";
  Status = MshcCommand (0, 0, CMD_INIT, 0, &R, NULL);
  if (EFI_ERROR (Status) && (Status != EFI_NO_RESPONSE)) {
    return Status;
  }

  *FailedStep = "CMD5 (no answer: module not powered, REG_ON or LPO)";
  Status = MshcCommand (5, 0, CMD_RESP_EXP, 0, &Ocr, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((
    DEBUG_ERROR,
    WIFI_LOG "CMD5 R4 0x%08x: %u function(s), memory %u, OCR 0x%06x\n",
    Ocr,
    R4_NUM_FUNCS (Ocr),
    (Ocr & R4_MEM_PRESENT) ? 1 : 0,
    Ocr & R4_OCR_MASK
    ));

  *FailedStep = "CMD5 ready";
  for (Try = 0; Try < 100; Try++) {
    Status = MshcCommand (5, (Ocr & R4_OCR_MASK & OCR_32_34) ? (Ocr & OCR_32_34) : (Ocr & R4_OCR_MASK), CMD_RESP_EXP, 0, &R, NULL);
    if (!EFI_ERROR (Status) && (R & R4_READY)) {
      break;
    }

    MicroSecondDelay (10000);
  }

  if (EFI_ERROR (Status) || ((R & R4_READY) == 0)) {
    return EFI_ERROR (Status) ? Status : EFI_TIMEOUT;
  }

  DEBUG ((DEBUG_INFO, WIFI_LOG "CMD5 ready after %u tries, R4 0x%08x\n", (UINT32)(Try + 1), R));

  *FailedStep = "CMD3";
  Status = MshcCommand (3, 0, CMD_RESP_EXP | CMD_RESP_CRC, 0, &R, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Rca = R >> 16;

  *FailedStep = "CMD7";
  Status = MshcCommand (7, Rca << 16, CMD_RESP_EXP | CMD_RESP_CRC, 0, &R, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((DEBUG_INFO, WIFI_LOG "RCA 0x%04x selected\n", Rca));

  *FailedStep = "CMD52 CCCR";
  Status = Cmd52 (FALSE, 0, SDIO_CCCR_REV, 0, &Rev);
  if (!EFI_ERROR (Status)) {
    Status = Cmd52 (FALSE, 0, SDIO_CCCR_CAPS, 0, &Caps);
  }

  Cis = 0;
  for (Try = 0; !EFI_ERROR (Status) && Try < 3; Try++) {
    Status = Cmd52 (FALSE, 0, SDIO_CCCR_CIS + (UINT32)Try, 0, &B);
    Cis |= (UINT32)B << (8 * Try);
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    WIFI_LOG "CCCR rev 0x%02x (CCCR %u, SDIO %u), caps 0x%02x, CIS at 0x%05x\n",
    Rev,
    Rev & 0xF,
    Rev >> 4,
    Caps,
    Cis
    ));

  *FailedStep = "CIS MANFID";
  Status = ReadManfid (Cis, &Vendor, &Device);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((
    DEBUG_ERROR,
    WIFI_LOG "CIS vendor 0x%04x device 0x%04x (%a)\n",
    Vendor,
    Device,
    ((Vendor == BCM_SDIO_VENDOR) && (Device == BCM43455_SDIO_DEVICE)) ? "BCM4345x, as expected" : "NOT the expected 0x02d0:0xa9bf"
    ));

  *FailedStep = "enable function 1";
  Status = Cmd52 (TRUE, 0, SDIO_CCCR_IOEX, BIT1, NULL);
  for (Try = 0; !EFI_ERROR (Status) && Try < 100; Try++) {
    Status = Cmd52 (FALSE, 0, SDIO_CCCR_IORX, 0, &B);
    if (!EFI_ERROR (Status) && (B & BIT1)) {
      break;
    }

    MicroSecondDelay (1000);
  }

  if (EFI_ERROR (Status) || ((B & BIT1) == 0)) {
    return EFI_ERROR (Status) ? Status : EFI_TIMEOUT;
  }

  *FailedStep = "ALP clock";
  Status = Cmd52 (TRUE, 1, SBSDIO_FUNC1_CHIPCLKCSR, SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_ALP_AVAIL_REQ, NULL);
  Csr    = 0;
  for (Try = 0; !EFI_ERROR (Status) && Try < 100; Try++) {
    Status = Cmd52 (FALSE, 1, SBSDIO_FUNC1_CHIPCLKCSR, 0, &Csr);
    if (!EFI_ERROR (Status) && (Csr & SBSDIO_ALP_AVAIL)) {
      break;
    }

    MicroSecondDelay (1000);
  }

  if (EFI_ERROR (Status) || ((Csr & SBSDIO_ALP_AVAIL) == 0)) {
    DEBUG ((DEBUG_INFO, WIFI_LOG "ChipClockCSR 0x%02x\n", Csr));
    return EFI_ERROR (Status) ? Status : EFI_TIMEOUT;
  }

  Status = Cmd52 (TRUE, 1, SBSDIO_FUNC1_CHIPCLKCSR, SBSDIO_FORCE_HW_CLKREQ_OFF | SBSDIO_FORCE_ALP, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *FailedStep = "chip ID (CMD53)";
  Status = BackplaneRead32 (SI_ENUM_BASE, &ChipId);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((
    DEBUG_ERROR,
    WIFI_LOG "chip ID 0x%08x: chip 0x%04x (%u) rev %u%a\n",
    ChipId,
    ChipId & 0xFFFF,
    ChipId & 0xFFFF,
    (ChipId >> 16) & 0xF,
    (((ChipId & 0xFFFF) == BCM4345_CHIP_ID) && (((ChipId >> 16) & 0xF) == 9)) ? ", a BCM43456 as expected" : ", NOT the expected 0x4345 rev 9"
    ));

  (VOID)Cmd52 (TRUE, 1, SBSDIO_FUNC1_CHIPCLKCSR, 0, NULL);
  *FailedStep = NULL;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
PlatformSdioWifiInit (
  VOID
  )
{
  EFI_STATUS   Status;
  CONST CHAR8  *FailedStep;

  WifiPinsAndClocks ();

  //
  // mmc-pwrseq-simple: REG_ON low (held from WifiPinsAndClocks), then high
  // and post-power-on-delay-ms = 200 before the first command.
  //
  WifiRegOn (FALSE, 10);
  WifiRegOn (TRUE, 200);
  DEBUG ((DEBUG_INFO, WIFI_LOG "WL_REG_ON high (GPIO1 PC6 reads %u)\n", GpioPinRead (WL_REG_ON_GROUP, WL_REG_ON_PIN)));

  Status = WifiProbe (&FailedStep);
  MshcQuiesce ();

  //
  // Hand the OS a chip as it is after power-on: whatever the probe did
  // (function 1 enabled, an RCA, the backplane window) is undone by a REG_ON
  // cycle, which costs 10 ms plus a settle the OS boot covers many times.
  //
  WifiRegOn (FALSE, 10);
  WifiRegOn (TRUE, 0);

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, WIFI_LOG "probe FAIL at %a: %r; WL_REG_ON left high\n", FailedStep, Status));
  } else {
    DEBUG ((DEBUG_ERROR, WIFI_LOG "probe PASS; WL_REG_ON cycled and left high, controller quiesced\n"));
  }

  return Status;
}

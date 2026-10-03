/** @file

  Copyright 2017, 2020 NXP
  Copyright 2021, Jared McNeill <jmcneill@invisible.ca>
  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/RockchipPlatformLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiLib.h>

#include <Protocol/OhciDeviceProtocol.h>
#include <Protocol/Usb2HostController.h>

#include <VarStoreData.h>
#include "UsbHcd.h"

STATIC
VOID
XhciSetBeatBurstLength (
  IN  UINTN  UsbReg
  )
{
  DWC3  *Dwc3Reg;

  Dwc3Reg = (VOID *)(UsbReg + DWC3_REG_OFFSET);

  MmioAndThenOr32 (
    (UINTN)&Dwc3Reg->GSBusCfg0,
    ~USB3_ENABLE_BEAT_BURST_MASK,
    USB3_ENABLE_BEAT_BURST
    );

  MmioOr32 ((UINTN)&Dwc3Reg->GSBusCfg1, USB3_SET_BEAT_BURST_LIMIT);
}

STATIC
VOID
Dwc3SetFladj (
  IN  DWC3    *Dwc3Reg,
  IN  UINT32  Val
  )
{
  MmioOr32 (
    (UINTN)&Dwc3Reg->GFLAdj,
    GFLADJ_30MHZ_REG_SEL |
    GFLADJ_30MHZ (Val)
    );
}

STATIC
VOID
Dwc3SetMode (
  IN  DWC3    *Dwc3Reg,
  IN  UINT32  Mode
  )
{
  MmioAndThenOr32 (
    (UINTN)&Dwc3Reg->GCtl,
    ~(DWC3_GCTL_PRTCAPDIR (DWC3_GCTL_PRTCAP_OTG)),
    DWC3_GCTL_PRTCAPDIR (Mode)
    );
}

/**
  This function issues phy reset and core soft reset

  @param  Dwc3Reg      Pointer to DWC3 register.

**/
STATIC
VOID
Dwc3CoreSoftReset (
  IN  DWC3  *Dwc3Reg
  )
{
  //
  // Put core in reset before resetting PHY
  //
  MmioOr32 ((UINTN)&Dwc3Reg->GCtl, DWC3_GCTL_CORESOFTRESET);

  //
  // Assert USB3 PIPE PHY soft reset (standard DWC3 core reset sequence).
  // Asserted for all controllers; cleared below after 100ms settle.
  // Per-controller SS availability is handled in XhciCoreInit:
  // SUSPHY is set before Dwc3SetMode(HOST) for HS-only controllers.
  //
  MmioOr32 ((UINTN)&Dwc3Reg->GUsb3PipeCtl[0], DWC3_GUSB3PIPECTL_PHYSOFTRST);

  //
  // Assert USB2 PHY reset (GUsb2PhyCfg PHYSOFTRST).
  //
  MmioOr32 ((UINTN)&Dwc3Reg->GUsb2PhyCfg[0], DWC3_GUSB2PHYCFG_PHYSOFTRST);

  //
  // Wait 100ms for PHY analog circuits to fully settle in reset state.
  // Linux kernel dwc3_core_soft_reset() uses mdelay(100) here.
  //
  MicroSecondDelay (100 * 1000);

  //
  // Clear USB3 PHY reset (transient reset pulse completed).
  //
  MmioAnd32 ((UINTN)&Dwc3Reg->GUsb3PipeCtl[0], ~DWC3_GUSB3PIPECTL_PHYSOFTRST);

  //
  // Clear USB2 PHY reset.
  //
  MmioAnd32 ((UINTN)&Dwc3Reg->GUsb2PhyCfg[0], ~DWC3_GUSB2PHYCFG_PHYSOFTRST);

  MemoryFence ();

  //
  // Take core out of reset, PHYs are stable now
  //
  MmioAnd32 ((UINTN)&Dwc3Reg->GCtl, ~DWC3_GCTL_CORESOFTRESET);
}

/**
  This function performs low-level initialization of DWC3 Core

  @param  Dwc3Reg      Pointer to DWC3 register.

**/
STATIC
EFI_STATUS
Dwc3CoreInit (
  IN  DWC3  *Dwc3Reg
  )
{
  UINT32  Revision;
  UINT32  Reg;
  UINTN   Dwc3Hwparams1;

  Revision = MmioRead32 ((UINTN)&Dwc3Reg->GSnpsId);
  //
  // This should read as 0x5533, ascii of U3(DWC_usb3) followed by revision num
  //
  if ((Revision & DWC3_GSNPSID_MASK) != DWC3_SYNOPSYS_ID) {
    DEBUG ((DEBUG_ERROR, "This is not a DesignWare USB3 DRD Core.\n"));
    return EFI_NOT_FOUND;
  }

  Dwc3CoreSoftReset (Dwc3Reg);

  Reg  = MmioRead32 ((UINTN)&Dwc3Reg->GCtl);
  Reg &= ~DWC3_GCTL_SCALEDOWN_MASK;
  Reg &= ~DWC3_GCTL_DISSCRAMBLE;

  Dwc3Hwparams1 = MmioRead32 ((UINTN)&Dwc3Reg->GHwParams1);

  if (DWC3_GHWPARAMS1_EN_PWROPT (Dwc3Hwparams1) ==
      DWC3_GHWPARAMS1_EN_PWROPT_CLK)
  {
    Reg &= ~DWC3_GCTL_DSBLCLKGTNG;
  } else {
    DEBUG ((DEBUG_WARN, "No power optimization available.\n"));
  }

  if ((Revision & DWC3_RELEASE_MASK) < DWC3_RELEASE_190a) {
    Reg |= DWC3_GCTL_U2RSTECN;
  }

  MmioWrite32 ((UINTN)&Dwc3Reg->GCtl, Reg);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
XhciCoreInit (
  IN  UINTN    UsbReg,
  IN  BOOLEAN  SuperSpeedEnabled
  )
{
  EFI_STATUS  Status;
  DWC3        *Dwc3Reg;

  Dwc3Reg = (VOID *)(UsbReg + DWC3_REG_OFFSET);

  Status = Dwc3CoreInit (Dwc3Reg);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "Dwc3CoreInit Failed for controller 0x%x (0x%r) \n",
      UsbReg,
      Status
      ));

    return Status;
  }

  if (!SuperSpeedEnabled) {
    /*
     * Set SUSPHY before HOST mode so the SS port never enters Rx.Detect.
     *
     * Linux dwc3 pattern (dwc3_core_init before dwc3_set_prtcap):
     *   if (max_speed < USB_SPEED_SUPER) reg |= SUSPHY;
     *
     * DWC3 PG §6.2.4.5: "When SUSPHY=1, the USB3 PHY port is unconditionally
     * placed in P3 and the SS port enters SSDisabled. Useful when USB3 PHY is
     * not connected."  SUSPHY alone achieves this without PHYSOFTRST:
     * adding PHYSOFTRST while SUSPHY is pending creates a deadlock on an
     * uninitialised PHY — PIPE3 is held in hard reset so P3_ACK never
     * arrives, the LTSSM stays in Polling and the HS companion port is
     * blocked from enumerating the USB-A device.
     */
    MmioOr32 ((UINTN)&Dwc3Reg->GUsb3PipeCtl[0], DWC3_GUSB3PIPECTL_SUSPHY);
    DEBUG ((DEBUG_WARN,
      "[USB] DWC3@0x%08x: SUSPHY set pre-HOST (SS→SSDisabled; SS PHY unavailable)"
      " — HS-only\n", UsbReg));
  }

  Dwc3SetMode (Dwc3Reg, DWC3_GCTL_PRTCAP_HOST);

  /*
   * HS-only controller (SS PHY not available for this DWC3): clear PP on the
   * SS port immediately after SetMode(HOST).
   *
   * Why this is necessary:
   *   SUSPHY=1 requests that the PHY enter P3, but if the PHY has no reference
   *   clock, P3_ACK never arrives.  The DWC3 LTSSM starts Rx.Detect/Polling
   *   as soon as PRTCAPDIR=HOST is written, before SUSPHY can take effect.
   *
   *   XhciDxe's XhcHaltHC writes USBCMD.RS=0 then waits up to 16 ms for
   *   USBSTS.HCH=1.  DWC3 will not assert HCH while the SS LTSSM is actively
   *   training (Polling state), causing XhcHaltHC to time-out and
   *   XhcDriverBindingStart to fail for this controller.
   *
   *   Setting PORTSC[0].PP=0 (xHCI §4.15.2.1) unconditionally powers off the
   *   SS port at the xHCI layer — no PHY cooperation needed.  The LTSSM stops
   *   immediately and DWC3 asserts HCH within microseconds of RS=0.
   *
   *   Note: XhciDxe's XhcResetHC (HCRST) will reset PORTSC back to defaults
   *   (PP=1) afterward, so the Polling LTSSM will restart post-HCRST.  The
   *   UsbHcProtocolNotify callback (fires after XhcRunHC via Install-
   *   ProtocolInterface) applies PP=0 a second time for the permanent fix.
   *   This is intentionally a two-stage approach.
   *
   *   RWC bits [23:17] must be written 0 to avoid accidentally clearing
   *   pending change bits on the HS companion port (port 1).
   */
  if (!SuperSpeedEnabled) {
    UINT8   CapLen = MmioRead8 (UsbReg);
    UINTN   OpBase = UsbReg + (UINTN)CapLen;
    UINT32  Ps     = MmioRead32 (OpBase + 0x400U);
    MmioWrite32 (OpBase + 0x400U,
                 Ps & ~((UINT32)(1u << 9) | (UINT32)(0x7Fu << 17)));  /* PP=0, RWC=0 */
    DEBUG ((DEBUG_WARN,
      "[USB] DWC3@0x%08x: PORTSC[0] PP cleared pre-XhciDxe (was 0x%08x now 0x%08x)"
      " — LTSSM stopped; XhcHaltHC will complete cleanly\n",
      UsbReg, Ps, MmioRead32 (OpBase + 0x400U)));
    /*
     * 20 ms settle: give the DWC3 internal FSM time to fully acknowledge the
     * PP=0 write and transition the SS port to Powered-Off before XhciDxe's
     * XhcHaltHC (RS=0 → wait HCH=1) runs.  Without this window, XhcHaltHC
     * may still see the LTSSM mid-transition and take much longer to assert
     * HCH, delaying XhcRunHC and shrinking the window in which UsbBusDxe can
     * observe CCS=1 on the HS companion port (PORTSC[1]).
     */
    MicroSecondDelay (20 * 1000);
  }

  Dwc3SetFladj (Dwc3Reg, GFLADJ_30MHZ_DEFAULT);

  /* UTMI+ mode */
  MmioAndThenOr32 ((UINTN)&Dwc3Reg->GUsb2PhyCfg[0], ~DWC3_GUSB2PHYCFG_USBTRDTIM_MASK, DWC3_GUSB2PHYCFG_USBTRDTIM (5));
  MmioOr32 ((UINTN)&Dwc3Reg->GUsb2PhyCfg[0], DWC3_GUSB2PHYCFG_PHYIF);

  /* snps,dis_enblslpm_quirk */
  MmioAndThenOr32 ((UINTN)&Dwc3Reg->GUsb2PhyCfg[0], ~DWC3_GUSB2PHYCFG_ENBLSLPM, 0);
  /* snps,dis-u2-freeclk-exists-quirk */
  MmioAndThenOr32 ((UINTN)&Dwc3Reg->GUsb2PhyCfg[0], ~DWC3_GUSB2PHYCFG_U2_FREECLK_EXISTS, 0);
  /* snps,dis_u2_susphy_quirk */
  MmioAndThenOr32 ((UINTN)&Dwc3Reg->GUsb2PhyCfg[0], ~DWC3_GUSB2PHYCFG_SUSPHY, 0);
  /* snps,dis-del-phy-power-chg-quirk */
  MmioAndThenOr32 ((UINTN)&Dwc3Reg->GUsb3PipeCtl[0], ~DWC3_GUSB3PIPECTL_DEPOCHANGE, 0);
  /* snps,dis_rxdet_inp3_quirk (GUSB3PIPECTL bit 28):
   * Prevent SS Rx.Detect when the pipe PHY is in P3 (low-power / SS.Inactive)
   * state. Without this bit the DWC3 may repeatedly assert Rx.Detect on a
   * pipe that is transitioning through P3, causing false CCS events.
   * Present in the vendor DTS for usb_drd1; applied here to all controllers.
   * This does NOT disable initial SS link training from Polling state. */
  MmioOr32 ((UINTN)&Dwc3Reg->GUsb3PipeCtl[0], DWC3_GUSB3PIPECTL_DISRXDETINP3);
  /* snps,dis-tx-ipgap-linecheck-quirk */
  MmioOr32 ((UINTN)&Dwc3Reg->GUctl1, DWC3_GUCTL1_TX_IPGAP_LINECHECK_DIS);
  /* snps,parkmode-disable-ss-quirk */
  MmioOr32 ((UINTN)&Dwc3Reg->GUctl1, DWC3_GUCTL1_PARKMODE_DISABLE_SS);
  /* snps,parkmode-disable-hs-quirk */
  MmioOr32 ((UINTN)&Dwc3Reg->GUctl1, DWC3_GUCTL1_PARKMODE_DISABLE_HS);

  /* Set max speed.
   * DCFG[2:0] (DEVSPD) is reserved/unused in HOST mode per DWC3 spec, but
   * setting it to the controller's actual capability makes the intent clear. */
  MmioAndThenOr32 ((UINTN)&Dwc3Reg->DCfg, ~DCFG_SPEED_MASK,
                   SuperSpeedEnabled ? DCFG_SPEED_SS : DCFG_SPEED_HS);

  /* snps,dis-u1-entry-quirk and snps,dis-u2-entry-quirk:
   * Disable SS link U1/U2 entry via GUCTL3 (offset 0xC60C from UsbReg).
   * GUCTL3 is not in the DWC3 struct; use direct MMIO. */
  MmioOr32 (UsbReg + DWC3_GUCTL3_OFFSET,
             DWC3_GUCTL3_DIS_U1_ENTRY | DWC3_GUCTL3_DIS_U2_ENTRY);

  return Status;
}

STATIC
VOID
DumpXhciPortsc (
  IN  UINTN  UsbReg
  )
{
  UINT8   CapLength;
  UINTN   OpBase;
  UINT32  HcSParams1;
  UINT32  MaxPorts;
  UINT32  Portsc;
  UINT32  Idx;

  CapLength  = MmioRead8 (UsbReg);           /* CAPLENGTH at CapBase+0, bits[7:0] */
  OpBase     = UsbReg + CapLength;
  HcSParams1 = MmioRead32 (UsbReg + 0x04);  /* HCSPARAMS1 at CapBase+0x04 */
  MaxPorts   = (HcSParams1 >> 24) & 0xFF;
  if (MaxPorts == 0 || MaxPorts > 16) {
    MaxPorts = 2;
  }

  DEBUG ((DEBUG_INFO,
    "XHCI[0x%lX]: CapLen=0x%x OpBase=0x%lX HCSPARAMS1=0x%08x MaxPorts=%d\n",
    UsbReg, CapLength, OpBase, HcSParams1, MaxPorts));
  Print (L"XHCI[0x%08x]: MaxPorts=%d\n", UsbReg, MaxPorts);

  for (Idx = 0; Idx < MaxPorts; Idx++) {
    Portsc = MmioRead32 (OpBase + 0x400 + Idx * 0x10);
    DEBUG ((DEBUG_INFO,
      "  PORTSC[%d]=0x%08x CCS=%d PED=%d PP=%d PLS=0x%x CSC=%d\n",
      Idx, Portsc,
      (Portsc >> 0) & 1,    /* CCS: Current Connect Status  */
      (Portsc >> 1) & 1,    /* PED: Port Enabled/Disabled   */
      (Portsc >> 9) & 1,    /* PP:  Port Power              */
      (Portsc >> 5) & 0xF,  /* PLS: Port Link State         */
      (Portsc >> 17) & 1    /* CSC: Connect Status Change   */
      ));
    Print (L"  PORTSC[%d]=0x%08x CCS=%d PP=%d PLS=0x%x\n",
      Idx, Portsc,
      (Portsc >> 0) & 1,
      (Portsc >> 9) & 1,
      (Portsc >> 5) & 0xF);
  }
}

EFIAPI
EFI_STATUS
InitializeXhciController (
  IN  NON_DISCOVERABLE_DEVICE  *This
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  UsbReg = This->Resources->AddrRangeMin;

  DEBUG ((DEBUG_INFO, "XHCI: Initialize DWC3 at 0x%lX\n", UsbReg));

  Status = XhciCoreInit (UsbReg, TRUE);

  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "XHCI: Controller init Failed for 0x%lX (0x%r)\n",
      UsbReg,
      Status
      ));
    return EFI_DEVICE_ERROR;
  }

  //
  // Change beat burst and outstanding pipelined transfers requests
  //
  XhciSetBeatBurstLength (UsbReg);

  DumpXhciPortsc (UsbReg);

  return EFI_SUCCESS;
}

#pragma pack (1)
typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  UINT32                      BaseAddress;
  EFI_DEVICE_PATH_PROTOCOL    End;
} OHCI_DEVICE_PATH;
#pragma pack ()

STATIC
EFI_STATUS
EFIAPI
RegisterOhciController (
  IN UINT32  BaseAddress
  )
{
  EFI_STATUS            Status;
  OHCI_DEVICE_PROTOCOL  *OhciDevice;
  OHCI_DEVICE_PATH      *OhciDevicePath;
  EFI_HANDLE            Handle;

  OhciDevice = (OHCI_DEVICE_PROTOCOL *)AllocateZeroPool (sizeof (*OhciDevice));
  if (OhciDevice == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  OhciDevice->BaseAddress = BaseAddress;

  OhciDevicePath = (OHCI_DEVICE_PATH *)CreateDeviceNode (
                                         HARDWARE_DEVICE_PATH,
                                         HW_VENDOR_DP,
                                         sizeof (*OhciDevicePath)
                                         );
  if (OhciDevicePath == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto FreeOhciDevice;
  }

  CopyGuid (&OhciDevicePath->Vendor.Guid, &gOhciDeviceProtocolGuid);

  /* Device paths must be unique */
  OhciDevicePath->BaseAddress = OhciDevice->BaseAddress;

  SetDevicePathNodeLength (
    &OhciDevicePath->Vendor,
    sizeof (*OhciDevicePath) - sizeof (OhciDevicePath->End)
    );
  SetDevicePathEndNode (&OhciDevicePath->End);

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiDevicePathProtocolGuid,
                  OhciDevicePath,
                  &gOhciDeviceProtocolGuid,
                  OhciDevice,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    goto FreeOhciDevicePath;
  }

  return EFI_SUCCESS;

FreeOhciDevicePath:
  FreePool (OhciDevicePath);
FreeOhciDevice:
  FreePool (OhciDevice);

  return Status;
}

/**
  This function gets registered as a callback to perform USB controller intialization

  @param  Event         Event whose notification function is being invoked.
  @param  Context       Pointer to the notification function's context.

**/
VOID
EFIAPI
UsbEndOfDxeCallback (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;
  UINT32      NumUsb2Controller;
  UINTN       XhciControllerAddrArraySize;
  UINT8       *XhciControllerAddrArrayPtr;
  UINT32      XhciControllerAddr;
  UINT32      EhciControllerAddr;
  UINT32      OhciControllerAddr;
  UINT32      Index;

  gBS->CloseEvent (Event);

  XhciControllerAddrArrayPtr  = PcdGetPtr (PcdDwc3BaseAddresses);
  XhciControllerAddrArraySize = PcdGetSize (PcdDwc3BaseAddresses);

  if (XhciControllerAddrArraySize % sizeof (UINT32) != 0) {
    DEBUG ((DEBUG_ERROR, "Invalid DWC3 address byte array size, skipping init.\n"));
    XhciControllerAddrArraySize = 0;
  }

  NumUsb2Controller = PcdGet32 (PcdNumEhciController);

  /* Enable USB PHYs */
  Usb2PhyResume ();

  UsbPortPowerEnable ();

  /* Register USB3 controllers.
   *
   * IMPORTANT: Do NOT rely solely on the NonDiscoverable init-callback
   * (InitializeXhciController) to initialize the DWC3 hardware.  In
   * practice the callback is only invoked by XhciDxe via PciIo->Attributes
   * for the FIRST registered controller; the second one (DWC3@0x23400000)
   * never received the callback, so it was left in reset.  Pre-initialize
   * every controller HERE, explicitly, before handing it to XhciDxe. */
  for (Index = 0; Index < XhciControllerAddrArraySize; Index += sizeof (UINT32)) {
    XhciControllerAddr = XhciControllerAddrArrayPtr[Index] |
                         XhciControllerAddrArrayPtr[Index + 1] << 8 |
                         XhciControllerAddrArrayPtr[Index + 2] << 16 |
                         XhciControllerAddrArrayPtr[Index + 3] << 24;

    /*
     * SS capability for RK3576/ROCK 4D:
     *   DWC3@0x23000000 (USB-C, DRD0) → Samsung USBDP combo PHY (u3phy0).
     *     UsbDpPhyDxe is RK3588-only and currently DISABLED for RK3576,
     *     so the SS PHY is not initialized → must run HS-only.
     *   DWC3@0x23400000 (USB-A, DRD1) → ComboPHY1 (Naneng).
     *     SS+HS when PcdComboPhy1Mode == COMBO_PHY_MODE_USB3 (default).
     *     HS-only when combphy1 is configured for PCIe or SATA (HII setting).
     */
    BOOLEAN  SsEnabled = (XhciControllerAddr == 0x23400000U) &&
                         (PcdGet32 (PcdComboPhy1Mode) == COMBO_PHY_MODE_USB3);

    Print (L"XHCI: init DWC3 @ 0x%08x (%s) ... ",
           XhciControllerAddr, SsEnabled ? L"SS+HS" : L"HS-only");
    DEBUG ((DEBUG_INFO, "XHCI: init DWC3 @ 0x%08x SsEnabled=%d\n",
            XhciControllerAddr, SsEnabled));

    Status = XhciCoreInit ((UINTN)XhciControllerAddr, SsEnabled);
    if (EFI_ERROR (Status)) {
      Print (L"FAILED (%r)\n", Status);
      DEBUG ((DEBUG_ERROR, "XHCI: DWC3 @ 0x%08x init failed: %r\n",
        XhciControllerAddr, Status));
    } else {
      XhciSetBeatBurstLength ((UINTN)XhciControllerAddr);
      Print (L"OK\n");
      DEBUG ((DEBUG_INFO, "XHCI: DWC3 @ 0x%08x init OK\n", XhciControllerAddr));
      DumpXhciPortsc ((UINTN)XhciControllerAddr);
    }

    /* Register with NULL InitFunc: DWC3 hardware is already initialized
     * above, so NonDiscoverablePciDeviceDxe just marks the device enabled. */
    Status = RegisterNonDiscoverableMmioDevice (
               NonDiscoverableDeviceTypeXhci,
               NonDiscoverableDeviceDmaTypeNonCoherent,
               NULL,
               NULL,
               1,
               XhciControllerAddr,
               PcdGet32 (PcdDwc3Size)
               );

    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "Failed to register XHCI device 0x%x, error %r\n",
        XhciControllerAddr,
        Status
        ));
    }
  }

  /* Register USB2 controllers */
  for (Index = 0; Index < NumUsb2Controller; Index++) {
    EhciControllerAddr = PcdGet32 (PcdEhciBaseAddress) +
                         (Index * (PcdGet32 (PcdEhciSize) + PcdGet32 (PcdOhciSize)));
    OhciControllerAddr = EhciControllerAddr + PcdGet32 (PcdOhciSize);

    Status = RegisterNonDiscoverableMmioDevice (
               NonDiscoverableDeviceTypeEhci,
               NonDiscoverableDeviceDmaTypeNonCoherent,
               NULL,
               NULL,
               1,
               EhciControllerAddr,
               PcdGet32 (PcdEhciSize)
               );

    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "Failed to register EHCI device 0x%x, error 0x%r \n",
        EhciControllerAddr,
        Status
        ));
    }

    Status = RegisterOhciController (OhciControllerAddr);

    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "Failed to register OHCI device 0x%x, error 0x%r \n",
        OhciControllerAddr,
        Status
        ));
    }
  }
}

/*
 * PcdDwc3BaseAddresses is a byte array of UINT32 controller base addresses.
 * Save the array pointer and size so the ReadyToBoot callback can re-dump
 * PORTSC after XhciDxe has started all controllers.
 */
STATIC UINT8   *gXhciAddrArray;
STATIC UINTN    gXhciAddrArraySize;

/* Registration token for gBS->LocateHandleBuffer(ByRegisterNotify, ...) */
STATIC VOID  *gUsb2HcRegistration;

/*
 * Bitmask tracking which gXhciAddrArray entries have already been matched by
 * UsbHcProtocolNotify.  Prevents the MaxPorts=2 tie (both DRD0 and DRD1 report
 * the same MaxPorts value) from causing every notification to match DRD0.
 * Bit N corresponds to gXhciAddrArray[N*4].  Set when matched; cleared only at
 * module load (static zero-init).
 */
STATIC UINTN  gXhciNotifiedMask = 0;

/*
 * RK3576 USB2PHY GRF status register addresses (absolute physical addresses).
 * utmi_ls   = bits[5:4]: 00=SE0/idle, 10=J(FS device), 01=K, 11=SE1
 * utmi_avalid = bit[1], utmi_bvalid = bit[0]
 *   GRF base 0x2602E000: u2phy0 status at +0x0080, u2phy1 status at +0x2080
 */
#define RK3576_U2PHY0_GRF_STATUS  0x2602E080UL
#define RK3576_U2PHY1_GRF_STATUS  0x26030080UL

/*
 * EFI_USB2_HC_PROTOCOL notification callback — functional fixup only.
 *
 * Fires synchronously inside XhcDriverBindingStart (at InstallProtocolInterface),
 * while XhciDxe is already emitting its own startup burst.  This callback must
 * produce ZERO debug output to avoid UART FIFO overflow that would swallow the
 * second controller's startup sequence.  All diagnostics go to ReadyToBoot.
 */
STATIC
VOID
EFIAPI
UsbHcProtocolNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS             Status;
  UINTN                  Count;
  EFI_HANDLE            *Handles;
  UINTN                  Hi;
  EFI_USB2_HC_PROTOCOL  *Usb2Hc;
  UINT8                  MaxSpeed;
  UINT8                  NumOfPort;
  UINT8                  Is64Bit;
  UINTN                  Index;
  UINTN                  MatchAddr;
  UINTN                  RawOpBase;
  UINT8                  RawCapLen;

  Count   = 0;
  Handles = NULL;
  Status  = gBS->LocateHandleBuffer (
                   ByRegisterNotify,
                   &gEfiUsb2HcProtocolGuid,
                   gUsb2HcRegistration,
                   &Count,
                   &Handles
                   );
  if (EFI_ERROR (Status) || (Count == 0) || (Handles == NULL)) {
    return;
  }

  for (Hi = 0; Hi < Count; Hi++) {
    Status = gBS->HandleProtocol (
                    Handles[Hi],
                    &gEfiUsb2HcProtocolGuid,
                    (VOID **)&Usb2Hc
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = Usb2Hc->GetCapability (Usb2Hc, &MaxSpeed, &NumOfPort, &Is64Bit);
    if (EFI_ERROR (Status)) {
      continue;
    }

    /*
     * Match this EFI_USB2_HC_PROTOCOL handle to its DWC3 MMIO base by
     * registration order.
     *
     * GetCapability() returns NumOfPort = number of USB2 HS ports (= 1 for
     * both DRD0 and DRD1).  HCSPARAMS1[31:24] is total xHCI ports including
     * SS (1 for DRD0 without UsbDpPhy, 2 for DRD1).  They count different
     * things — comparing them produces an ambiguous or wrong match.
     *
     * XhciDxe calls InstallProtocolInterface in the same order as
     * RegisterNonDiscoverableMmioDevice was called, which follows
     * gXhciAddrArray order (DRD0=0x23000000 first, DRD1=0x23400000 second).
     * EFI_USB2_HC_PROTOCOL notifications therefore fire in that same order.
     * Claim the first unclaimed entry on each notification.
     */
    MatchAddr = 0;
    for (Index = 0; Index < gXhciAddrArraySize; Index += sizeof (UINT32)) {
      UINTN  ArrIdx = Index / sizeof (UINT32);
      if (!(gXhciNotifiedMask & (1u << ArrIdx))) {
        MatchAddr          = gXhciAddrArray[Index]                       |
                             (UINTN)gXhciAddrArray[Index + 1] << 8   |
                             (UINTN)gXhciAddrArray[Index + 2] << 16  |
                             (UINTN)gXhciAddrArray[Index + 3] << 24;
        gXhciNotifiedMask |= (1u << ArrIdx);
        break;
      }
    }

    if (MatchAddr == 0) {
      continue;
    }

    RawCapLen = MmioRead8 (MatchAddr);
    RawOpBase = MatchAddr + RawCapLen;

    /*
     * Keep this callback silent.  It fires synchronously inside XhciDxe's
     * InstallProtocolInterface, which is also emitting its own startup burst
     * (XhcCreateUsb3Hc, XhcResetHC, XhcInitSched lines).  Any DEBUG output
     * we add here competes for the 1.5 Mbaud UART FIFO and causes overflow
     * that swallows the second controller's entire startup sequence.
     *
     * Functional work only — no diagnostics.  Full register dumps happen in
     * UsbReadyToBootCallback after all XhciDxe/UsbBusDxe activity is done.
     */

    if (MatchAddr == 0x23000000U) {
      /*
       * DRD0 (0x23000000, USB-C): wait for stable device connection.
       *
       * Bus-powered portable HDDs go through an internal init cycle after
       * VBUS is applied: the USB bridge chip appears briefly, then disconnects
       * and reconnects once the storage layer (SATA spin-up, SCSI init) is
       * ready.  If BDS ConnectAll fires during this reconnect window the
       * xHCI Configure Endpoint command times out, the UsbBusDxe enumeration
       * fails, and no boot option is created for the drive.
       *
       * Poll PORTSC[0].CCS (bit 0) here for up to USB_CCS_POLL_MS milliseconds.
       * Once CCS=1 is seen, wait USB_CCS_STABLE_MS more for the bridge chip /
       * HDD spindle to complete its internal reset and present a stable
       * connection before UsbBusDxe starts enumeration.
       *
       * No debug output — this callback fires synchronously inside XhciDxe's
       * InstallProtocolInterface and must not flood the 1.5 Mbaud UART FIFO.
       */
#define USB_CCS_POLL_MS    5000U   /* max wait for device to appear    */
#define USB_CCS_STABLE_MS  3000U   /* extra settle time once CCS=1     */
      {
        UINTN   HsPortscAddr = RawOpBase + 0x0400U;  /* PORTSC[0] HS port */
        UINTN   PollMs;
        for (PollMs = 0; PollMs < USB_CCS_POLL_MS; PollMs++) {
          if ((MmioRead32 (HsPortscAddr) & 0x1U) != 0) {
            break;  /* device physically present — move to settle phase */
          }
          MicroSecondDelay (1000U);  /* 1 ms per iteration */
        }
        /* If a device was seen, give it time to finish its internal init */
        if ((MmioRead32 (HsPortscAddr) & 0x1U) != 0) {
          MicroSecondDelay (USB_CCS_STABLE_MS * 1000U);
        }
      }
    }

    if (MatchAddr == 0x23400000U) {
      if (PcdGet32 (PcdComboPhy1Mode) != COMBO_PHY_MODE_USB3) {
        /*
         * combphy1 not in USB3 mode (PCIe/SATA/unconnected): SS PHY
         * has no reference clock so Polling never completes.  Kill SS
         * port PP=0 and re-assert SUSPHY so UsbBusDxe sees CCS=0 on
         * port 0 and falls through cleanly to the HS companion port.
         * RWC bits [23:17] written 0 to preserve pending CSC/PEC on port 1.
         */
        UINTN  SsPortscAddr = RawOpBase + 0x0400U;
        UINT32 SsPortsc     = MmioRead32 (SsPortscAddr);
        MmioWrite32 (SsPortscAddr,
                     SsPortsc & ~((UINT32)(1u << 9) |      /* PP=0 */
                                  (UINT32)(0x7Fu << 17))); /* RWC=0 */
        MmioOr32 ((UINTN)(MatchAddr + 0xC2C0UL), DWC3_GUSB3PIPECTL_SUSPHY);
      }
      /*
       * combphy1 in USB3 mode: leave SS port alone.  XhciCoreInit did not
       * set SUSPHY, so the LTSSM is free to train.  A connected device will
       * have CCS=1 / PLS=Polling here; enumeration continues normally.
       */
    }
  }

  FreePool (Handles);
}

STATIC
VOID
EFIAPI
UsbReadyToBootCallback (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN                 Index;
  UINT32                Addr;
  UINT32                Sts;
  UINT8                 RawCapLen;
  UINT32                RawHcSParams1;
  UINT8                 RawMaxPorts;
  UINTN                 RawOpBase;
  UINT32                RawPortsc;
  UINT32                Pi;
  UINTN                 NumHandles;
  EFI_HANDLE           *HandleBuffer;

  gBS->CloseEvent (Event);

  DEBUG ((DEBUG_WARN, "\n[USB-RTB] === ReadyToBoot USB diagnostics ===\n"));

  /* 1. Count EFI_USB2_HC_PROTOCOL handles */
  NumHandles = 0;
  HandleBuffer = NULL;
  gBS->LocateHandleBuffer (
         ByProtocol, &gEfiUsb2HcProtocolGuid, NULL, &NumHandles, &HandleBuffer);
  DEBUG ((DEBUG_WARN,
    "[USB-RTB] EFI_USB2_HC_PROTOCOL handles: %u (expect 2)\n", (UINT32)NumHandles));
  if (HandleBuffer != NULL) {
    FreePool (HandleBuffer);
    HandleBuffer = NULL;
  }

  /* 2. Raw MMIO PORTSC dump - one line per port, always explicit.
   * Iterate in REVERSE so DRD1 (USB-A, last entry) prints first — the
   * UEFI menu can appear within ~6 lines and would otherwise cut DRD1 data.
   */
  DEBUG ((DEBUG_WARN, "[USB-RTB] --- Raw MMIO PORTSC ---\n"));
  for (Index = gXhciAddrArraySize - sizeof (UINT32);
       (INTN)Index >= 0;
       Index -= sizeof (UINT32)) {
    Addr = gXhciAddrArray[Index] |
           (UINT32)gXhciAddrArray[Index + 1] << 8 |
           (UINT32)gXhciAddrArray[Index + 2] << 16 |
           (UINT32)gXhciAddrArray[Index + 3] << 24;

    RawCapLen    = MmioRead8 (Addr);
    RawHcSParams1 = MmioRead32 (Addr + 0x04);
    RawOpBase    = Addr + RawCapLen;
    RawMaxPorts  = (RawHcSParams1 >> 24) & 0xFF;
    if (RawMaxPorts == 0 || RawMaxPorts > 4) {
      RawMaxPorts = 2;
    }

    DEBUG ((DEBUG_WARN, "[USB-RTB] 0x%08x CapLen=%d MaxPorts=%d\n",
      Addr, RawCapLen, RawMaxPorts));
    for (Pi = 0; Pi < RawMaxPorts; Pi++) {
      RawPortsc = MmioRead32 (RawOpBase + 0x400 + Pi * 0x10);
      DEBUG ((DEBUG_WARN,
        "[USB-RTB]   PORTSC[%u]=0x%08x CCS=%d PED=%d PP=%d PLS=0x%x CSC=%d\n",
        Pi, RawPortsc,
        (RawPortsc >>  0) & 1,   /* CCS */
        (RawPortsc >>  1) & 1,   /* PED */
        (RawPortsc >>  9) & 1,   /* PP  */
        (RawPortsc >>  5) & 0xF, /* PLS */
        (RawPortsc >> 17) & 1)); /* CSC */
    }

    /* DWC3 global register state at ReadyToBoot:
     * GUSB3PIPECTL[0] = UsbReg+0xC2C0, GUSB2PHYCFG[0] = UsbReg+0xC200
     * GDbgLtssm      = UsbReg+0xC164  (SS LTSSM debug state)
     */
    {
      UINT32 G3P = MmioRead32 (Addr + 0xC2C0U);
      UINT32 G2P = MmioRead32 (Addr + 0xC200U);
      UINT32 GLT = MmioRead32 (Addr + 0xC164U);
      UINT32 GCT = MmioRead32 (Addr + 0xC110U);
      DEBUG ((DEBUG_WARN,
        "[USB-RTB]   GCTL=0x%08x PRTCAP=%d  GUSB3PIPECTL=0x%08x PHYSOFTRST=%d DISRXDETINP3=%d SUSPHY=%d\n",
        GCT, (GCT >> 12) & 3,
        G3P, (G3P >> 31) & 1, (G3P >> 28) & 1, (G3P >> 17) & 1));
      DEBUG ((DEBUG_WARN,
        "[USB-RTB]   GUSB2PHYCFG=0x%08x PHYSOFTRST=%d SUSPHY=%d  GDbgLtssm=0x%08x LTDBSTATE=0x%x LTDBSUB=0x%x\n",
        G2P, (G2P >> 31) & 1, (G2P >> 6) & 1,
        GLT, (GLT >> 18) & 0xF, (GLT >> 22) & 0xF));
    }
  }

  /* 3. USB2PHY UTMI line-state — u2phy1 (USB-A) printed first */
  Sts = MmioRead32 (RK3576_U2PHY1_GRF_STATUS);
  DEBUG ((DEBUG_WARN,
    "[USB-RTB] u2phy1 GRF[0x80]=0x%08x utmi_ls=%d bvalid=%d avalid=%d\n",
    Sts, (Sts >> 4) & 3, Sts & 1, (Sts >> 1) & 1));

  Sts = MmioRead32 (RK3576_U2PHY0_GRF_STATUS);
  DEBUG ((DEBUG_WARN,
    "[USB-RTB] u2phy0 GRF[0x80]=0x%08x utmi_ls=%d bvalid=%d avalid=%d\n",
    Sts, (Sts >> 4) & 3, Sts & 1, (Sts >> 1) & 1));

  DEBUG ((DEBUG_WARN, "[USB-RTB] === done ===\n"));
}

/*
 * USB_GRF_USB3OTG0_CON1 (USB GRF 0x2601E000 + 0x30): the U3 port of DRD0, the
 * USB-C controller at 0x23000000. Mainline phy-rockchip-usbdp.c writes 0x1100
 * to enable it and 0x0188 to disable it (its usb3otg0_cfg macro lists them as
 * "disable, enable", but rk_udphy_u3_port_disable() passes its argument as the
 * enable flag, so the names read backwards; naneng-combphy's u3otg*_port_en
 * confirms 0x1100 = enabled).
 *
 * The board comes up with 0x0188, and then DRD0's xHCI reports MaxPorts = 1
 * while its USB3 Supported Protocol capability still says "ports from 2,
 * count 0". UEFI's XhciDxe and Linux skip a zero count; Windows' usbxhci
 * rejects it and fails the controller with STATUS_INVALID_PARAMETER (code 10).
 * With 0x1100 the capability reads "port 2, count 1", MaxPorts = 2, and it is
 * live (no controller reset needed).
 *
 * Written at ExitBootServices rather than at init: UEFI runs DRD0 HS-only
 * because the USBDP PHY is never initialised here, and that path works with
 * the port disabled. Measured on CM5-IO, 2026-10-01: with this written after
 * UEFI's USB init, Windows started XHC0 and enumerated a device on the USB-C
 * port. The SS port has no PHY behind it, so nothing will train at 5 Gb/s on
 * it; Linux rewrites this register itself from its usbdp driver.
 */
#define RK3576_USB_GRF_USB3OTG0_CON1  0x2601E030UL
#define RK3576_USB3OTG0_U3_PORT_EN    0x1100U

/*
 * The USB3 half of the RK3576 USB/DP combo PHY (the PHY behind DRD0's U3
 * port), brought up the way mainline's phy-rockchip-usbdp.c rk_udphy_init()
 * does for UDPHY_MODE_USB. Addresses and bits are from mainline rk3576.dtsi,
 * clk-rk3576.c and rst-rk3576.c.
 *
 * Why: enabling DRD0's U3 port with no PHY behind it (above) made Windows
 * start XHC0, but every WinPE shutdown then bugchecked with
 * WHEA_INTERNAL_ERROR (9, 0x11): usbxhci's read of port 2's PORTSC, taken
 * while UsbHub3 suspended the root hub, was answered with a synchronous
 * external abort. The abort record carried 0x23000000; hiding XHC0 from the
 * OS made the bugcheck go away (2 of 2 runs, against 5 of 5 before).
 * With the PHY's PLL locked, the SS port has a PIPE clock behind it.
 *
 * Orientation is not known here (the CC controller is an FUSB302 on I2C0 that
 * UEFI does not drive), so the lanes are set up unflipped: a SuperSpeed
 * device trains in one plug orientation and falls back to high speed in the
 * other. All four lanes are muxed to USB (mainline's USB-only mode).
 */
#define RK3576_PMU1CRU_BASE           0x27220000UL
#define RK3576_PMU1CRU_GATE_CON0      (RK3576_PMU1CRU_BASE + 0x800)
#define RK3576_PMU1CRU_SOFTRST_CON00  (RK3576_PMU1CRU_BASE + 0xA00)
#define RK3576_PMU1CRU_SOFTRST_CON01  (RK3576_PMU1CRU_BASE + 0xA04)

#define UDPHY_PCLK_GATE               BIT12   // PCLK_USBDPPHY
#define UDPHY_IMMORTAL_GATE           BIT15   // CLK_USBDP_COMBO_PHY_IMMORTAL
#define UDPHY_RST_PMA_APB             BIT12   // SRST_P_USBDPPHY, SOFTRST_CON00
#define UDPHY_RST_INIT                BIT15   // SRST_USBDP_COMBO_PHY_INIT, SOFTRST_CON00
#define UDPHY_RST_CMN                 BIT0    // SOFTRST_CON01
#define UDPHY_RST_LANE                BIT1    // SOFTRST_CON01
#define UDPHY_RST_PCS_APB             BIT2    // SOFTRST_CON01

#define RK3576_PMU0_GRF_OSC_CON6      (0x26024000UL + 0x18)
#define CLK_PHY_REF_SRC_SEL           BIT4    // 0 = xin24m

#define RK3576_USBDPPHY_GRF_CON1      (0x2602C000UL + 0x0004)
#define UDPHY_GRF_LOW_PWRN            BIT13
#define UDPHY_GRF_RX_LFPS             BIT14

#define RK3576_UDPHY_PMA              (0x2B010000UL + 0x8000)
#define CMN_LANE_MUX_AND_EN_OFFSET    0x0288
#define CMN_DP_LANE_MUX_N(n)          (1U << ((n) + 4))
#define CMN_ANA_LCPLL_DONE_OFFSET     0x0350
#define CMN_ANA_LCPLL_LOCK_DONE       BIT7
#define CMN_ANA_LCPLL_AFC_DONE        BIT6

#define HIWORD(Mask, Val)             (((UINT32)(Mask) << 16) | (Val))

STATIC CONST UINT16  mUdphyInitSequence[][2] = {
  { 0x0104, 0x44 }, { 0x0234, 0xe8 }, { 0x0248, 0x44 }, { 0x028c, 0x18 },
  { 0x081c, 0xe5 }, { 0x0878, 0x00 }, { 0x0994, 0x1c }, { 0x0af0, 0x00 },
  { 0x181c, 0xe5 }, { 0x1878, 0x00 }, { 0x1994, 0x1c }, { 0x1af0, 0x00 },
  { 0x0428, 0x60 }, { 0x0d58, 0x33 }, { 0x1d58, 0x33 }, { 0x0990, 0x74 },
  { 0x0d64, 0x17 }, { 0x08c8, 0x13 }, { 0x1990, 0x74 }, { 0x1d64, 0x17 },
  { 0x18c8, 0x13 }, { 0x0d90, 0x40 }, { 0x0da8, 0x40 }, { 0x0dc0, 0x40 },
  { 0x0dd8, 0x40 }, { 0x1d90, 0x40 }, { 0x1da8, 0x40 }, { 0x1dc0, 0x40 },
  { 0x1dd8, 0x40 }, { 0x03c0, 0x30 }, { 0x03c4, 0x06 }, { 0x0e10, 0x00 },
  { 0x1e10, 0x00 }, { 0x043c, 0x0f }, { 0x0d2c, 0xff }, { 0x1d2c, 0xff },
  { 0x0d34, 0x0f }, { 0x1d34, 0x0f }, { 0x08fc, 0x2a }, { 0x0914, 0x28 },
  { 0x0a30, 0x03 }, { 0x0e38, 0x03 }, { 0x0ecc, 0x27 }, { 0x0ed0, 0x22 },
  { 0x0ed4, 0x26 }, { 0x18fc, 0x2a }, { 0x1914, 0x28 }, { 0x1a30, 0x03 },
  { 0x1e38, 0x03 }, { 0x1ecc, 0x27 }, { 0x1ed0, 0x22 }, { 0x1ed4, 0x26 },
  { 0x0048, 0x0f }, { 0x0060, 0x3c }, { 0x0064, 0xf7 }, { 0x006c, 0x20 },
  { 0x0070, 0x7d }, { 0x0074, 0x68 }, { 0x0af4, 0x1a }, { 0x1af4, 0x1a },
  { 0x0440, 0x3f }, { 0x10d4, 0x08 }, { 0x20d4, 0x08 }, { 0x00d4, 0x30 },
  { 0x0024, 0x6e },
};

STATIC CONST UINT16  mUdphy24mRefclkCfg[][2] = {
  { 0x0090, 0x68 }, { 0x0094, 0x68 }, { 0x0128, 0x24 }, { 0x012c, 0x44 },
  { 0x0130, 0x3f }, { 0x0134, 0x44 }, { 0x015c, 0xa9 }, { 0x0160, 0x71 },
  { 0x0164, 0x71 }, { 0x0168, 0xa9 }, { 0x0174, 0xa9 }, { 0x0178, 0x71 },
  { 0x017c, 0x71 }, { 0x0180, 0xa9 }, { 0x018c, 0x41 }, { 0x0190, 0x00 },
  { 0x0194, 0x05 }, { 0x01ac, 0x2a }, { 0x01b0, 0x17 }, { 0x01b4, 0x17 },
  { 0x01b8, 0x2a }, { 0x01c8, 0x04 }, { 0x01cc, 0x08 }, { 0x01d0, 0x08 },
  { 0x01d4, 0x04 }, { 0x01d8, 0x20 }, { 0x01dc, 0x01 }, { 0x01e0, 0x09 },
  { 0x01e4, 0x03 }, { 0x01f0, 0x29 }, { 0x01f4, 0x02 }, { 0x01f8, 0x02 },
  { 0x01fc, 0x29 }, { 0x0208, 0x2a }, { 0x020c, 0x17 }, { 0x0210, 0x17 },
  { 0x0214, 0x2a }, { 0x0224, 0x20 }, { 0x03f0, 0x0a }, { 0x03f4, 0x07 },
  { 0x03f8, 0x07 }, { 0x03fc, 0x0c }, { 0x0404, 0x12 }, { 0x0408, 0x1a },
  { 0x040c, 0x1a }, { 0x0410, 0x3f }, { 0x0ce0, 0x68 }, { 0x0ce8, 0xd0 },
  { 0x0cf0, 0x87 }, { 0x0cf8, 0x70 }, { 0x0d00, 0x70 }, { 0x0d08, 0xa9 },
  { 0x1ce0, 0x68 }, { 0x1ce8, 0xd0 }, { 0x1cf0, 0x87 }, { 0x1cf8, 0x70 },
  { 0x1d00, 0x70 }, { 0x1d08, 0xa9 }, { 0x0a3c, 0xd0 }, { 0x0a44, 0xd0 },
  { 0x0a48, 0x01 }, { 0x0a4c, 0x0d }, { 0x0a54, 0xe0 }, { 0x0a5c, 0xe0 },
  { 0x0a64, 0xa8 }, { 0x1a3c, 0xd0 }, { 0x1a44, 0xd0 }, { 0x1a48, 0x01 },
  { 0x1a4c, 0x0d }, { 0x1a54, 0xe0 }, { 0x1a5c, 0xe0 }, { 0x1a64, 0xa8 },
};

/**
  Bring up the USB3 half of the USBDP PHY. MMIO and stalls only, so it is
  safe in an ExitBootServices notification.

  @retval TRUE   LCPLL locked.
  @retval FALSE  It did not, or the reference clock is not 24 MHz.
**/
STATIC
BOOLEAN
Rk3576UsbDpPhyUsb3InitOnce (
  IN BOOLEAN  Flip
  )
{
  UINTN   Index;
  UINT32  Val;

  Val = 0;
  if ((MmioRead32 (RK3576_PMU0_GRF_OSC_CON6) & CLK_PHY_REF_SRC_SEL) != 0) {
    // Only the 24 MHz table is carried; the other parent is cpll-derived.
    DEBUG ((DEBUG_ERROR, "UsbDpPhy: refclk is not xin24m, not touching the PHY\n"));
    return FALSE;
  }

  // Clocks on (the gates are set-to-gate).
  MmioWrite32 (RK3576_PMU1CRU_GATE_CON0,
               HIWORD (UDPHY_PCLK_GATE | UDPHY_IMMORTAL_GATE, 0));

  // rk_udphy_reset_assert_all(), then 10 ms.
  MmioWrite32 (RK3576_PMU1CRU_SOFTRST_CON00,
               HIWORD (UDPHY_RST_PMA_APB | UDPHY_RST_INIT,
                       UDPHY_RST_PMA_APB | UDPHY_RST_INIT));
  MmioWrite32 (RK3576_PMU1CRU_SOFTRST_CON01,
               HIWORD (UDPHY_RST_CMN | UDPHY_RST_LANE | UDPHY_RST_PCS_APB,
                       UDPHY_RST_CMN | UDPHY_RST_LANE | UDPHY_RST_PCS_APB));
  MicroSecondDelay (10000);

  // RX LFPS for USB, then step 1: power on the PMA, release the APB resets.
  MmioWrite32 (RK3576_USBDPPHY_GRF_CON1, HIWORD (UDPHY_GRF_RX_LFPS, UDPHY_GRF_RX_LFPS));
  MmioWrite32 (RK3576_USBDPPHY_GRF_CON1, HIWORD (UDPHY_GRF_LOW_PWRN, UDPHY_GRF_LOW_PWRN));
  MmioWrite32 (RK3576_PMU1CRU_SOFTRST_CON00, HIWORD (UDPHY_RST_PMA_APB, 0));
  MmioWrite32 (RK3576_PMU1CRU_SOFTRST_CON01, HIWORD (UDPHY_RST_PCS_APB, 0));

  // Step 2: init sequence and the 24 MHz reference clock.
  for (Index = 0; Index < ARRAY_SIZE (mUdphyInitSequence); Index++) {
    MmioWrite32 (RK3576_UDPHY_PMA + mUdphyInitSequence[Index][0], mUdphyInitSequence[Index][1]);
  }

  for (Index = 0; Index < ARRAY_SIZE (mUdphy24mRefclkCfg); Index++) {
    MmioWrite32 (RK3576_UDPHY_PMA + mUdphy24mRefclkCfg[Index][0], mUdphy24mRefclkCfg[Index][1]);
  }

  // Step 3: lane mux, as mainline's rk_udphy_set_typec_default_mapping():
  // unflipped, lanes 0/1 carry USB and 2/3 are muxed to DP; flipped, the
  // other way round. No DP lane is enabled.
  MmioAndThenOr32 (
    RK3576_UDPHY_PMA + CMN_LANE_MUX_AND_EN_OFFSET,
    ~(UINT32)0xFF,
    Flip ? (CMN_DP_LANE_MUX_N (0) | CMN_DP_LANE_MUX_N (1))
         : (CMN_DP_LANE_MUX_N (2) | CMN_DP_LANE_MUX_N (3))
    );

  // Step 4: release init, 200 ns; step 5: release cmn and lane.
  MmioWrite32 (RK3576_PMU1CRU_SOFTRST_CON00, HIWORD (UDPHY_RST_INIT, 0));
  MicroSecondDelay (1);
  MmioWrite32 (RK3576_PMU1CRU_SOFTRST_CON01, HIWORD (UDPHY_RST_CMN | UDPHY_RST_LANE, 0));

  // Step 6: LCPLL lock, 100 ms as mainline. The RX CDR lock it also polls
  // needs a link partner and only ever logs, so it is not waited for here.
  for (Index = 0; Index < 500; Index++) {
    Val = MmioRead32 (RK3576_UDPHY_PMA + CMN_ANA_LCPLL_DONE_OFFSET);
    if (((Val & CMN_ANA_LCPLL_AFC_DONE) != 0) && ((Val & CMN_ANA_LCPLL_LOCK_DONE) != 0)) {
      DEBUG ((DEBUG_INFO, "UsbDpPhy: USB3 LCPLL locked (0x%02x) after %u us, %a\n",
              Val, Index * 200, Flip ? "flipped" : "unflipped"));
      return TRUE;
    }

    MicroSecondDelay (200);
  }

  DEBUG ((DEBUG_ERROR, "UsbDpPhy: USB3 LCPLL did not lock (0x%02x), %a\n",
          Val, Flip ? "flipped" : "unflipped"));
  return FALSE;
}

/**
  Rk3576UsbDpPhyUsb3InitOnce, retried.

  Mainline knows this PLL can fail to lock on the first attempt when earlier
  software already had it running, and answers with -EPROBE_DEFER so the
  whole init runs again (rk_udphy_status_check). It happened here on
  2026-10-03: a reset after a boot that had locked it (0xDE) gave 0x38 on
  the next one. Each attempt starts by asserting every PHY reset.
**/
STATIC
BOOLEAN
Rk3576UsbDpPhyUsb3Init (
  VOID
  )
{
  UINTN  Attempt;

  //
  // The plug orientation is not known here, and it seems to matter to more
  // than link training: on CM5-IO with a dock plugged in one way round the
  // LCPLL never locked (3 boots of 3, 0x38, all four lanes muxed to USB),
  // while the other way round it locked within 200 us. Try the unflipped
  // mapping, then the flipped one, and keep whichever locks; the log says
  // which, so this guess can be checked against the dock's orientation.
  //
  for (Attempt = 1; Attempt <= 4; Attempt++) {
    if (Rk3576UsbDpPhyUsb3InitOnce ((Attempt % 2) == 0)) {
      if (Attempt > 1) {
        DEBUG ((DEBUG_INFO, "UsbDpPhy: locked on attempt %u\n", Attempt));
      }

      return TRUE;
    }

    if ((MmioRead32 (RK3576_PMU0_GRF_OSC_CON6) & CLK_PHY_REF_SRC_SEL) != 0) {
      return FALSE;   // not a lock problem; retrying will not help
    }
  }

  return FALSE;
}

STATIC
VOID
EFIAPI
UsbExitBootServicesCallback (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  UINTN  Index;
  UINTN  Base;

  //
  // The U3 port only with its PHY running: an enabled SS port with nothing
  // behind it is what made Windows take the synchronous external abort. If
  // the PHY does not come up, leave the port disabled and let Windows fail
  // XHC0 (code 10) rather than bugcheck.
  //
  if (Rk3576UsbDpPhyUsb3Init ()) {
    MmioWrite32 (RK3576_USB_GRF_USB3OTG0_CON1,
                 (0xFFFFU << 16) | RK3576_USB3OTG0_U3_PORT_EN);
  }

  //
  // Hand both PHY-suspend enables to the OS set, as Linux runs them.
  //
  // UEFI clears GUSB2PHYCFG.SUSPHY (the RK3588 vendor DT's
  // dis_u2_susphy_quirk) and leaves GUSB3PIPECTL.SUSPHY clear on DRD1 so its
  // SS link can train. Mainline rk3576.dtsi has neither susphy quirk, and on
  // CM5-IO under Linux both DRDs read GUSB2PHYCFG 0x00101448 and
  // GUSB3PIPECTL 0x..0a0002, bits 6 and 17 set; ours were 0x00101408 and
  // 0x11080002. The databook asks for SUSPHY=1 once the core is initialised.
  //
  // Without them, Windows bugchecked on every WinPE shutdown
  // (WHEA_INTERNAL_ERROR 9/0x11, a synchronous external abort): once UsbHub3
  // had put DRD1's SS link to the onboard hub into U3, usbxhci's read of
  // port 2's PORTSC (xHCI +0x430) faulted. Linux reads the same register in
  // the same U3 state without trouble. Measured 2026-10-03, n=1 dump.
  //
  for (Index = 0; Index + sizeof (UINT32) <= gXhciAddrArraySize; Index += sizeof (UINT32)) {
    Base = gXhciAddrArray[Index] |
           (UINTN)gXhciAddrArray[Index + 1] << 8 |
           (UINTN)gXhciAddrArray[Index + 2] << 16 |
           (UINTN)gXhciAddrArray[Index + 3] << 24;
    MmioOr32 (Base + DWC3_REG_OFFSET + 0x100, DWC3_GUSB2PHYCFG_SUSPHY);   // GUSB2PHYCFG(0)
    MmioOr32 (Base + DWC3_REG_OFFSET + 0x1C0, DWC3_GUSB3PIPECTL_SUSPHY);  // GUSB3PIPECTL(0)
  }
}

/**
  The Entry Point of module. It follows the standard UEFI driver model.

  @param[in] ImageHandle   The firmware allocated handle for the EFI image.
  @param[in] SystemTable   A pointer to the EFI System Table.

  @retval EFI_SUCCESS      The entry point is executed successfully.
  @retval other            Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
InitializeUsbHcd (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   EndOfDxeEvent;
  EFI_EVENT   ReadyToBootEvent;
  EFI_EVENT   ExitBootServicesEvent;

  gXhciAddrArray     = PcdGetPtr (PcdDwc3BaseAddresses);
  gXhciAddrArraySize = PcdGetSize (PcdDwc3BaseAddresses);
  if (gXhciAddrArraySize % sizeof (UINT32) != 0) {
    gXhciAddrArraySize = 0;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  UsbEndOfDxeCallback,
                  NULL,
                  &gEfiEndOfDxeEventGroupGuid,
                  &EndOfDxeEvent
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EfiCreateEventReadyToBootEx (
             TPL_CALLBACK,
             UsbReadyToBootCallback,
             NULL,
             &ReadyToBootEvent
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  UsbExitBootServicesCallback,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &ExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /*
   * Register a notification that fires synchronously when XhciDxe installs
   * EFI_USB2_HC_PROTOCOL (inside XhcDriverBindingStart), before UsbBusDxe
   * connects.  The callback injects a port reset so that PRC=1 is set in
   * PORTSC when UsbBusDxe makes its first GetRootHubPortStatus call, ensuring
   * XhcInitializeDeviceSlot is called and device enumeration succeeds.
   */
  {
    EFI_EVENT  Usb2HcEvent;

    Status = gBS->CreateEvent (
                    EVT_NOTIFY_SIGNAL,
                    TPL_CALLBACK,
                    UsbHcProtocolNotify,
                    NULL,
                    &Usb2HcEvent
                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = gBS->RegisterProtocolNotify (
                    &gEfiUsb2HcProtocolGuid,
                    Usb2HcEvent,
                    &gUsb2HcRegistration
                    );
  }

  return Status;
}

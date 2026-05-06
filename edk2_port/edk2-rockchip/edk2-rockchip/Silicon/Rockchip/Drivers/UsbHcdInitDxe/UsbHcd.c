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
      "[USB] DWC3@0x%08x: SUSPHY set pre-HOST (SS→SSDisabled; combphy1 not init)"
      " — HS-only via u2phy1\n", UsbReg));
  }

  Dwc3SetMode (Dwc3Reg, DWC3_GCTL_PRTCAP_HOST);

  /*
   * HS-only controller (combphy1 uninitialised): clear PP on the SS port
   * immediately after SetMode(HOST).
   *
   * Why this is necessary:
   *   SUSPHY=1 requests that the PHY enter P3, but combphy1 has no reference
   *   clock so P3_ACK never arrives.  The DWC3 LTSSM starts Rx.Detect/Polling
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
     * SS capability: DWC3@0x23000000 (USB-C) has Samsung USBDP PHY
     * initialized by UsbDpPhyDxe → SS capable.
     * DWC3@0x23400000 (USB-A) has combphy1 (Naneng) which is NEVER
     * initialized by U-Boot SPL or TF-A → HS-only.
     * XhciCoreInit handles SS disable internally via SUSPHY+PHYSOFTRST
     * when SuperSpeedEnabled = FALSE.  No post-init patching needed.
     */
    BOOLEAN  SsEnabled = (XhciControllerAddr == 0x23000000U);

    Print (L"XHCI: init DWC3 @ 0x%08x (%s) ... ",
           XhciControllerAddr, SsEnabled ? L"SS+HS" : L"HS-only (combphy1 N/A)");
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

    /*
     * DRD1 (0x23400000, USB-A): SS port PORTSC[0].PP=0 — permanent fix.
     *
     * XhciCoreInit already cleared PP before XhciDxe started, but HCRST
     * (inside XhcResetHC) resets PORTSC to defaults (PP=1), which restarts
     * the Polling LTSSM on the uninitialized combphy1.  UsbBusDxe would then
     * see CCS=1 on the SS port and spin 10 s waiting for PRC after issuing
     * PortReset — starving HS companion port (PORTSC[1]) enumeration.
     *
     * Apply PP=0 here (post-HCRST, post-XhcRunHC) so UsbBusDxe's first
     * GetRootHubPortStatus call sees CCS=0 on port 0 and skips it cleanly.
     * RWC bits [23:17] written 0 to preserve any pending CSC/PEC on port 1.
     */
    if (MatchAddr == 0x23400000U) {
      UINTN  SsPortscAddr = RawOpBase + 0x0400U;
      UINT32 SsPortsc     = MmioRead32 (SsPortscAddr);
      MmioWrite32 (SsPortscAddr,
                   SsPortsc & ~((UINT32)(1u << 9) |      /* PP=0 */
                                (UINT32)(0x7Fu << 17))); /* RWC=0 */
      /* Re-assert SUSPHY in case HCRST cleared it */
      MmioOr32 ((UINTN)(MatchAddr + 0xC2C0UL), DWC3_GUSB3PIPECTL_SUSPHY);
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

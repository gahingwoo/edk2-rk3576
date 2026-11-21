/** @file
 *
 *  Flattened Device Tree platform driver
 *
 *  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/RockchipDsiPanel.h>
#include <Protocol/SimpleFileSystem.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/FdtLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/Rk3576Pcie.h>
#include <Library/RockchipPlatformLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Guid/Fdt.h>
#include <Guid/FileInfo.h>

#include <VarStoreData.h>

#include <dt-bindings/clock/rockchip,rk3576-cru.h>
#include <dt-bindings/power/rockchip,rk3576-power.h>

// Expand and stringize
#define XS(x)  S(x)
#define S(x)   #x

#define MAX_PATH_LENGTH  512

STATIC  VOID  *mPlatformFdt;
STATIC  VOID  *mLoadedImageEventRegistration;

STATIC
INTN
EFIAPI
StriCmp (
  IN CONST  CHAR16  *String1,
  IN CONST  CHAR16  *String2
  )
{
  while ((*String1 != L'\0') &&
         (CharToUpper (*String1) == CharToUpper (*String2)))
  {
    String1++;
    String2++;
  }

  return CharToUpper (*String1) - CharToUpper (*String2);
}

STATIC
BOOLEAN
EFIAPI
StrEndsWith (
  IN CONST  CHAR16  *String,
  IN CONST  CHAR16  *Extension
  )
{
  UINTN  StringLength    = StrLen (String);
  UINTN  ExtensionLength = StrLen (Extension);

  if (StringLength < ExtensionLength) {
    return FALSE;
  }

  return StriCmp (String + StringLength - ExtensionLength, Extension) == 0;
}

STATIC
EFI_STATUS
EFIAPI
FdtOpenIntoAlloc (
  IN OUT  VOID   **Fdt,
  IN OUT  VOID   **DestinationFdt  OPTIONAL,
  IN      UINTN  Size
  )
{
  VOID  *NewFdt;
  INTN  Ret;

  NewFdt = AllocatePool (Size);
  if (NewFdt == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "FdtPlatform: Not enough resources to reallocate FDT (%d bytes).\n",
      Size
      ));
    return EFI_OUT_OF_RESOURCES;
  }

  Ret = FdtOpenInto (*Fdt, NewFdt, Size);
  if (Ret) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: Failed to copy FDT. Ret=%a\n", FdtStrerror (Ret)));
    FreePool (NewFdt);
    return EFI_LOAD_ERROR;
  }

  if (DestinationFdt != NULL) {
    *DestinationFdt = NewFdt;
  } else {
    FreePool (*Fdt);
    *Fdt = NewFdt;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FdtEnableNode (
  IN         VOID     *Fdt,
  IN CONST   CHAR8    *NodePath,
  IN         BOOLEAN  Enable
  )
{
  INT32  Node;
  INT32  Ret;
  CHAR8  *NodeStatus;

  Node = FdtPathOffset (Fdt, NodePath);
  if (Node < 0) {
    DEBUG ((
      DEBUG_ERROR,
      "FdtPlatform: Couldn't locate FDT node path '%a'. Ret=%a\n",
      NodePath,
      FdtStrerror (Node)
      ));
    return EFI_NOT_FOUND;
  }

  NodeStatus = Enable ? "okay" : "disabled";
  Ret        = FdtSetPropString (Fdt, Node, "status", NodeStatus);
  if (Ret) {
    DEBUG ((
      DEBUG_ERROR,
      "FdtPlatform: Failed to set '%a' status to '%a'. Ret=%a\n",
      NodePath,
      NodeStatus,
      FdtStrerror (Ret)
      ));
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
FdtFixupComboPhyDevices (
  IN VOID  *Fdt
  )
{
  UINT32      Index;
  INT32       Node;
  INT32       Ret;
  CONST VOID  *Property;
  INT32       Length;

  DEBUG ((DEBUG_INFO, "FdtPlatform: Fixing up Combo PHY devices (PCIe, SATA, USB)\n"));

  struct {
    UINT32    Mode;
    CHAR8     *PcieNodePath;
    CHAR8     *SataNodePath;
  } Phys[] = {
    { PcdGet32 (PcdComboPhy0ModeDefault), "/pcie@22000000", "/sata@2a240000" },
    { PcdGet32 (PcdComboPhy1ModeDefault), "/pcie@22400000", "/sata@2a250000" },
  };

  for (Index = 0; Index < ARRAY_SIZE (Phys); Index++) {
    FdtEnableNode (
      Fdt,
      Phys[Index].PcieNodePath,
      Phys[Index].Mode == COMBO_PHY_MODE_PCIE
      );

    FdtEnableNode (
      Fdt,
      Phys[Index].SataNodePath,
      Phys[Index].Mode == COMBO_PHY_MODE_SATA
      );

    //
    // For M.2 slots supporting both PCIe and SATA, we must reference
    // the 3V3 supply in SATA mode as well, to prevent the kernel from
    // turning it off.
    //
    if (Phys[Index].Mode == COMBO_PHY_MODE_SATA) {
      Node = FdtPathOffset (Fdt, Phys[Index].PcieNodePath);
      if (Node < 0) {
        continue;
      }

      Property = FdtGetProp (Fdt, Node, "vpcie3v3-supply", &Length);
      if (Property == NULL) {
        continue;
      }

      ASSERT (Length == sizeof (UINT32));

      Node = FdtPathOffset (Fdt, Phys[Index].SataNodePath);
      if (Node < 0) {
        continue;
      }

      Ret = FdtSetProp (Fdt, Node, "phy-supply", Property, Length);
      if (Ret < 0) {
        DEBUG ((
          DEBUG_ERROR,
          "FdtPlatform: Failed to set 'phy-supply' property for '%a'. Ret=%a\n",
          Phys[Index].SataNodePath,
          FdtStrerror (Ret)
          ));
        continue;
      }
    }
  }
}

/* RK3576 has no PCIe 3.0 controller - FdtFixupPcie3Devices() not needed */

STATIC
VOID
EFIAPI
FdtFixupChosen (
  IN VOID  *Fdt
  )
{
  //
  // Defensive: when the OS bootloader (GRUB / systemd-boot via efistub /
  // direct EFI LoadImage) forwards a kernel command line, Linux uses that
  // and the value of /chosen/bootargs is ignored.  But if the user boots
  // without any cmdline source (eg. raw kernel image launched from the
  // UEFI shell, or some installers that rely solely on the FDT), the
  // kernel will end up with NO console, since UART output is gated by
  // either `earlycon` or `console=ttyS0,...` on the cmdline.
  //
  // Inject a default `bootargs` that enables earlycon on UART0 (RK3576
  // serial0 alias = uart0 @ 0x2ad40000, 1500000 baud) -- ONLY if chosen
  // doesn't already carry one.  This makes "OS booted but silent" much
  // less likely without ever overriding a user-supplied cmdline.
  //
  STATIC CONST CHAR8  DefaultBootArgs[] =
    "earlycon=uart8250,mmio32,0x2ad40000 console=ttyS0,1500000n8 console=tty1";

  INT32       Chosen;
  INT32       Ret;
  CONST VOID  *Existing;
  INT32       Len;

  Chosen = FdtPathOffset (Fdt, "/chosen");
  if (Chosen < 0) {
    Chosen = FdtAddSubnode (Fdt, FdtPathOffset (Fdt, "/"), "chosen");
    if (Chosen < 0) {
      DEBUG ((DEBUG_ERROR, "FdtPlatform: Failed to add /chosen. Ret=%a\n", FdtStrerror (Chosen)));
      return;
    }
  }

  Existing = FdtGetProp (Fdt, Chosen, "bootargs", &Len);
  if ((Existing != NULL) && (Len > 1)) {
    DEBUG ((
      DEBUG_INFO,
      "FdtPlatform: /chosen/bootargs already present (\"%a\"), keeping it.\n",
      (CHAR8 *)Existing
      ));
    return;
  }

  Ret = FdtSetPropString (Fdt, Chosen, "bootargs", (CHAR8 *)DefaultBootArgs);
  if (Ret < 0) {
    DEBUG ((
      DEBUG_ERROR,
      "FdtPlatform: Failed to set default bootargs. Ret=%a\n",
      FdtStrerror (Ret)
      ));
    return;
  }

  DEBUG ((
    DEBUG_INFO,
    "FdtPlatform: Injected default bootargs=\"%a\"\n",
    DefaultBootArgs
    ));
}

STATIC
VOID
EFIAPI
FdtFixupPcieResources (
  IN VOID  *Fdt
  )
{
  STATIC CHAR8  *PcieNodes[] = {
    [PCIE_SEGMENT_PCIE0] = "/pcie@22000000",
    [PCIE_SEGMENT_PCIE1] = "/pcie@22400000",
  };

  UINT32  Segment;
  CHAR8   *NodePath;
  INT32   Node;
  INT32   Ret;

  DEBUG ((DEBUG_INFO, "FdtPlatform: Fixing up PCIe resources\n"));

  for (Segment = 0; Segment < ARRAY_SIZE (PcieNodes); Segment++) {
    NodePath = PcieNodes[Segment];

    Node = FdtPathOffset (Fdt, NodePath);
    if (Node < 0) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Couldn't locate '%a' node. Ret=%a\n",
        NodePath,
        FdtStrerror (Node)
        ));
      continue;
    }

    UINT32  Ranges[][7] = {
      {
        CpuToFdt32 (0x01000000), // I/O space
        CpuToFdt32 ((UINT32)((UINT64)PCIE_IO_BUS_BASE >> 32)),
        CpuToFdt32 ((UINT32)(PCIE_IO_BUS_BASE)),
        CpuToFdt32 ((UINT32)((UINT64)PCIE_IO_BASE (Segment) >> 32)),
        CpuToFdt32 ((UINT32)(PCIE_IO_BASE (Segment))),
        CpuToFdt32 ((UINT32)((UINT64)PCIE_IO_SIZE >> 32)),
        CpuToFdt32 ((UINT32)(PCIE_IO_SIZE))
      },
      {
        CpuToFdt32 (0x02000000), // 32-bit non-prefetchable memory
        CpuToFdt32 ((UINT32)((UINT64)PCIE_MEM32_BUS_BASE >> 32)),
        CpuToFdt32 ((UINT32)(PCIE_MEM32_BUS_BASE)),
        CpuToFdt32 ((UINT32)((UINT64)PCIE_MEM32_BASE (Segment) >> 32)),
        CpuToFdt32 ((UINT32)(PCIE_MEM32_BASE (Segment))),
        CpuToFdt32 ((UINT32)((UINT64)PCIE_MEM32_SIZE >> 32)),
        CpuToFdt32 ((UINT32)(PCIE_MEM32_SIZE))
      },
      {
        CpuToFdt32 (0x43000000), // 64-bit prefetchable memory
        CpuToFdt32 ((UINT32)((UINT64)PCIE_MEM64_BASE (Segment) >> 32)),
        CpuToFdt32 ((UINT32)(PCIE_MEM64_BASE (Segment))),
        CpuToFdt32 ((UINT32)((UINT64)PCIE_MEM64_BASE (Segment) >> 32)),
        CpuToFdt32 ((UINT32)(PCIE_MEM64_BASE (Segment))),
        CpuToFdt32 ((UINT32)((UINT64)PCIE_MEM64_SIZE >> 32)),
        CpuToFdt32 ((UINT32)(PCIE_MEM64_SIZE))
      },
    };
    Ret = FdtSetProp (Fdt, Node, "ranges", Ranges, sizeof (Ranges));
    if (Ret < 0) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Failed to set 'ranges' property for '%a'. Ret=%a\n",
        NodePath,
        FdtStrerror (Ret)
        ));
      continue;
    }

    UINT32  BusRange[] = {
      CpuToFdt32 (PCIE_BUS_BASE (Segment)),
      CpuToFdt32 (PCIE_BUS_LIMIT (Segment))
    };
    Ret = FdtSetProp (Fdt, Node, "bus-range", BusRange, sizeof (BusRange));
    if (Ret < 0) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Failed to set 'bus-range' property for '%a'. Ret=%a\n",
        NodePath,
        FdtStrerror (Ret)
        ));
      continue;
    }

    CHAR8  *RidMapProps[] = { "msi-map", "iommu-map" };
    for (UINT32 RidMapIndex = 0; RidMapIndex < ARRAY_SIZE (RidMapProps); RidMapIndex++) {
      UINT32  *RidMap;
      INT32   RidMapLength;

      RidMap = (UINT32 *)FdtGetProp (Fdt, Node, RidMapProps[RidMapIndex], &RidMapLength);
      if ((RidMap == NULL) || (RidMapLength != sizeof (UINT32) * 4)) {
        DEBUG ((
          DEBUG_ERROR,
          "FdtPlatform: Failed to get '%a' property for '%a'. Ret=%a\n",
          RidMapProps[RidMapIndex],
          NodePath,
          FdtStrerror ((RidMapLength < 0) ? RidMapLength : -FDT_ERR_BADVALUE)
          ));
        continue;
      }

      RidMap[0] = CpuToFdt32 (PCIE_BUS_BASE (Segment) << 8);
      RidMap[2] = RidMap[0];
      RidMap[3] = CpuToFdt32 (PCIE_BUS_COUNT << 8);
    }
  }
}

STATIC
VOID
EFIAPI
FdtFixupVopDevices (
  IN VOID  *Fdt
  )
{
  //
  // This fixup allows Linux to reuse the UEFI GOP framebuffer in cases
  // where the kernel lacks native support for display output.
  //
  // It works by:
  // 1) disabling VOP device nodes to prevent any existing drivers from
  //    resetting the hardware.
  // 2) disabling related power domain nodes to prevent the kernel from
  //    turning them off for being otherwise unused.
  // 3) creating fake "regulator-fixed-clock" nodes whose sole purpose is
  //    to reference and keep the required clocks enabled - again, to
  //    prevent the kernel from gating them.
  //

  STATIC CHAR8  *VopNodesToDisable[] = {
    // Mainline kernel DTS puts peripheral nodes under /soc/; the
    // /display-subsystem composite node lives at the root in both DTB styles.
    "/soc/vop@27d00000",
    "/soc/iommu@27d07e00",
    "/soc/hdmi@27da0000",
    "/soc/hdmiphy@2b000000",   // Samsung HDPTX PHY — disable to prevent probe hang
    "/display-subsystem",
    //
    // Disable the PD_VOP power-domain DT node so that rockchip-pm-domain does
    // NOT register it (or its children, including PD_USB) with genpd.
    //
    // Background: PD_USB (RK3576_PD_USB) is a DT sub-node of PD_VOP
    // (RK3576_PD_VOP).  With VOP/HDMI nodes disabled (force-GOP mode), genpd
    // sees PD_VOP as having no consumers and powers it off at ~t=2s via
    // "PM: genpd: Disabling unused power domains".  This takes PD_USB down at
    // the hardware level, making the DWC3@23000000 MMIO inaccessible.
    //
    // When the PD_VOP DT node is disabled, rockchip-pm-domain skips both
    // PD_VOP and its children entirely.  The hardware stays at the UEFI-
    // initialized state (powered on).  DWC3 no longer has a power-domains
    // reference (removed in FdtFixupUsbDrd0) and probes cleanly without any
    // genpd interaction.
    //
    "/soc/power-management@27380000/power-controller/power-domain@" XS (RK3576_PD_VOP),
  };

  STATIC UINT32  VopRequiredCruClocks[] = {
    ACLK_VOP,
    HCLK_VOP,
    DCLK_VP0,
    DCLK_VP1,
    DCLK_VP2,
    PCLK_VOP_ROOT,
    //
    // A MIPI DSI screen the firmware lit: the DSI host, its D-PHY and the
    // PWM behind the backlight.  The mainline device tree has the first two
    // disabled and no node at all for the third, so nothing else holds
    // their clocks, and "clk: Disabling unused clocks" took the picture
    // away and left the backlight on.  Their power domain, PD_VO0, is a
    // child of PD_VOP and goes unmanaged with it.
    //
    PCLK_DSIHOST0,
    CLK_DSIHOST0,
    PCLK_MIPI_DCPHY,
    CLK_PHY_REF_SRC,
    CLK_PWM2,
    PCLK_PWM2,
  };

  UINTN  Index;
  INT32  Root;
  INT32  Node;
  INT32  Ret;
  INT32  CruPhandle;

  if (!PcdGet8 (PcdFdtForceGop)) {
    return;
  }

  DEBUG ((DEBUG_INFO, "FdtPlatform: Fixing up VOP devices (force GOP)\n"));

  for (Index = 0; Index < ARRAY_SIZE (VopNodesToDisable); Index++) {
    FdtEnableNode (Fdt, VopNodesToDisable[Index], FALSE);
  }

  Root = FdtPathOffset (Fdt, "/");
  ASSERT (Root >= 0);
  if (Root < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: Couldn't locate FDT root. Ret=%a\n", FdtStrerror (Root)));
    return;
  }

  Node = FdtPathOffset (Fdt, "/soc/clock-controller@27200000");
  if (Node < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: Couldn't locate CRU node. Ret=%a\n", FdtStrerror (Node)));
    return;
  }

  CruPhandle = FdtGetPhandle (Fdt, Node);
  if (CruPhandle <= 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: Failed to get CRU phandle.\n"));
    return;
  }

  for (Index = 0; Index < ARRAY_SIZE (VopRequiredCruClocks); Index++) {
    UINT32  ClockId = VopRequiredCruClocks[Index];

    CHAR8  NodeName[34];
    AsciiSPrint (NodeName, sizeof (NodeName), "clk-cru-%d-keep-alive-reg", ClockId);

    Node = FdtAddSubnode (Fdt, Root, NodeName);
    if (Node < 0) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Couldn't create FDT node '%a'. Ret=%a\n",
        NodeName,
        FdtStrerror (Node)
        ));
      return;
    }

    Ret = FdtSetPropString (Fdt, Node, "compatible", "regulator-fixed-clock");
    if (Ret < 0) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Failed to set 'compatible' property for '%a'. Ret=%a\n",
        NodeName,
        FdtStrerror (Ret)
        ));
      return;
    }

    Ret = FdtSetPropString (Fdt, Node, "regulator-name", NodeName);
    if (Ret < 0) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Failed to set 'regulator-name' property for '%a'. Ret=%a\n",
        NodeName,
        FdtStrerror (Ret)
        ));
      return;
    }

    Ret = FdtSetPropEmpty (Fdt, Node, "regulator-always-on");
    if (Ret < 0) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Failed to set 'regulator-always-on' property for '%a'. Ret=%a\n",
        NodeName,
        FdtStrerror (Ret)
        ));
      return;
    }

    UINT32  ClockPair[] = { CpuToFdt32 (CruPhandle), CpuToFdt32 (ClockId) };
    Ret = FdtSetProp (Fdt, Node, "clocks", ClockPair, sizeof (ClockPair));
    if (Ret < 0) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Failed to set 'clocks' property for '%a'. Ret=%a\n",
        NodeName,
        FdtStrerror (Ret)
        ));
      return;
    }
  }
}

STATIC
VOID
EFIAPI
FdtFixupUfsDevices (
  IN VOID  *Fdt
  )
{
  //
  // The UFS host controller (ufshc @ 0x2A2D0000) shares PD_USB with DWC3 DRD0.
  // The Fedora kernel driver (ufshcd-rockchip) fails to probe on this board
  // (no UFS storage is populated) and its deferred probe timeout leaves
  // PD_USB in an indeterminate state.  Disable the node so the kernel does not
  // attempt to probe UFS at all, keeping PD_USB cleanly owned by DWC3 alone.
  //
  DEBUG ((DEBUG_INFO, "FdtPlatform: Disabling UFS host controller (no UFS storage)\n"));
  FdtEnableNode (Fdt, "/soc/ufshc@2a2d0000", FALSE);
}

STATIC
VOID
EFIAPI
FdtFixupNorFlashDevices (
  IN VOID  *Fdt
  )
{
  //
  // UEFI owns the SPI NOR flash controller (sfc0 @ 0x2A340000) via
  // NorFlashDxe for EFI variable storage.  The hardware is left active
  // after ExitBootServices and the kernel spi_rockchip_sfc module causes
  // an asynchronous SError when it tries to probe the same MMIO range.
  // Disable the node so the kernel leaves the SFC hardware untouched.
  //
  DEBUG ((DEBUG_INFO, "FdtPlatform: Disabling SFC NOR flash node (UEFI-owned)\n"));
  FdtEnableNode (Fdt, "/soc/spi@2a340000", FALSE);
}

STATIC
VOID
EFIAPI
FdtFixupUsbDrd0 (
  IN VOID  *Fdt
  )
{
  INT32  Node;
  INT32  Ret;

  //
  // The Samsung USBDP combo PHY (phy@2b010000, compatible:
  // "rockchip,rk3576-usbdp-phy") is supported by the mainline Linux kernel
  // driver phy-rockchip-usbdp.c.  DWC3 DRD0 (usb@23000000) retains its full
  // "phys" list referencing both u2phy0_otg (USB2) and usbdp_phy (USB3 SS),
  // enabling SuperSpeed operation on the USB-C port.
  //
  // Note: when UEFI has previously initialised the USBDP PHY, the kernel's
  // LCPLL lock check may time out on the first probe attempt and return
  // -EPROBE_DEFER.  The driver handles this by retrying, so the second probe
  // succeeds.  No special DT fixup is required for this.
  //
  // DisplayPort Alt Mode is NOT enabled; it requires a FUSB302 Type-C PD
  // controller and the full Type-C mux/switch stack, which are not yet
  // wired up in the board DTS.
  //

  //
  // Remove power-domains from usb@23000000, but only in force-GOP mode.
  //
  // PD_USB is a hardware child of PD_VOP.  In force-GOP mode the VOP, HDMI and
  // PD_VOP nodes are all disabled, so nothing claims PD_VOP, genpd powers it
  // off, and the hardware cuts the whole subtree.  DWC3's genpd reference
  // cannot help there: it names a domain whose DT node is disabled.  Dropping
  // it leaves the kernel treating DWC3 as always powered, which UEFI has left
  // it.
  //
  // Outside force-GOP mode that reference is the only thing keeping PD_VOP up
  // until rockchipdrm loads from udev, well after genpd_power_off_unused().
  // Removing it there cost the board its root filesystem: PD_VOP went down at
  // 1.66 s, DWC3 hit dead MMIO, and the abort killed the deferred-probe worker,
  // taking NVMe with it.
  //
  if (!PcdGet8 (PcdFdtForceGop)) {
    return;
  }

  Node = FdtPathOffset (Fdt, "/soc/usb@23000000");
  if (Node >= 0) {
    Ret = FdtDelProp (Fdt, Node, "power-domains");
    if ((Ret < 0) && (Ret != -FDT_ERR_NOTFOUND)) {
      DEBUG ((DEBUG_ERROR, "FdtPlatform: Failed to remove usb@23000000 power-domains: %a\n", FdtStrerror (Ret)));
    } else {
      DEBUG ((DEBUG_INFO, "FdtPlatform: Removed power-domains from usb@23000000\n"));
    }
  }
}

/**
  In force-GOP mode, describe the picture the firmware left running as a
  "simple-framebuffer" node under /chosen.

  A kernel finds the same framebuffer through the EFI GOP, but only one that
  was built with efifb or with SYSFB_SIMPLEFB draws on it; Armbian's is built
  with neither and showed nothing but the boot loader's last frame.  The
  device tree node binds simplefb or simpledrm directly.

  The node needs the screen's mode, so it can only be written once the
  consoles are connected: at ReadyToBoot, and when a device tree override is
  loaded.  A rotated GOP reports the two dimensions swapped and offers only
  Blt, while the framebuffer stays in the panel's native scan, which is what
  an OS gets; describe that.
**/
STATIC
VOID
FdtFixupSimpleFramebuffer (
  IN VOID  *Fdt
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL          *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  UINT32                                Width;
  UINT32                                Height;
  UINT32                                Stride;
  UINT64                                Base;
  UINT64                                Size;
  UINT32                                Cells[4];
  UINT32                                Value;
  CHAR8                                 Name[40];
  INT32                                 Chosen;
  INT32                                 Node;
  INT32                                 Ret;

  if (!PcdGet8 (PcdFdtForceGop)) {
    return;
  }

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop)) ||
      (Gop->Mode == NULL) || (Gop->Mode->Info == NULL) || (Gop->Mode->FrameBufferBase == 0))
  {
    return;
  }

  Info = Gop->Mode->Info;
  switch (Info->PixelFormat) {
    case PixelBlueGreenRedReserved8BitPerColor:
      Width  = Info->HorizontalResolution;
      Height = Info->VerticalResolution;
      Stride = Info->PixelsPerScanLine * 4;
      break;
    case PixelBltOnly:
      // Rotated menus: the framebuffer is the native scan, the other way up.
      Width  = Info->VerticalResolution;
      Height = Info->HorizontalResolution;
      Stride = Width * 4;
      break;
    default:
      return;
  }

  Base = Gop->Mode->FrameBufferBase;
  Size = (UINT64)Stride * Height;

  Chosen = FdtPathOffset (Fdt, "/chosen");
  if (Chosen < 0) {
    return;
  }

  AsciiSPrint (Name, sizeof (Name), "framebuffer@%lx", Base);
  if (FdtSubnodeOffset (Fdt, Chosen, Name) >= 0) {
    return;
  }

  //
  // The node has a reg, so /chosen needs the cells and a ranges to make it
  // an address in the root's space.  The root of every RK3576 tree is 2/2.
  //
  Value = CpuToFdt32 (2);
  Ret   = FdtSetProp (Fdt, Chosen, "#address-cells", &Value, sizeof (Value));
  if (Ret >= 0) {
    Ret = FdtSetProp (Fdt, Chosen, "#size-cells", &Value, sizeof (Value));
  }

  if (Ret >= 0) {
    Ret = FdtSetPropEmpty (Fdt, Chosen, "ranges");
  }

  if (Ret < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: simple-framebuffer: /chosen cells: %a\n", FdtStrerror (Ret)));
    return;
  }

  Chosen = FdtPathOffset (Fdt, "/chosen");
  Node   = FdtAddSubnode (Fdt, Chosen, Name);
  if (Node < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: simple-framebuffer: add node: %a\n", FdtStrerror (Node)));
    return;
  }

  Cells[0] = CpuToFdt32 ((UINT32)(Base >> 32));
  Cells[1] = CpuToFdt32 ((UINT32)Base);
  Cells[2] = CpuToFdt32 ((UINT32)(Size >> 32));
  Cells[3] = CpuToFdt32 ((UINT32)Size);

  // Byte order in memory is B, G, R, X: a little-endian XRGB8888.
  Ret = FdtSetPropString (Fdt, Node, "compatible", "simple-framebuffer");
  if (Ret >= 0) {
    Ret = FdtSetProp (Fdt, Node, "reg", Cells, sizeof (Cells));
  }

  if (Ret >= 0) {
    Value = CpuToFdt32 (Width);
    Ret   = FdtSetProp (Fdt, Node, "width", &Value, sizeof (Value));
  }

  if (Ret >= 0) {
    Value = CpuToFdt32 (Height);
    Ret   = FdtSetProp (Fdt, Node, "height", &Value, sizeof (Value));
  }

  if (Ret >= 0) {
    Value = CpuToFdt32 (Stride);
    Ret   = FdtSetProp (Fdt, Node, "stride", &Value, sizeof (Value));
  }

  if (Ret >= 0) {
    Ret = FdtSetPropString (Fdt, Node, "format", "x8r8g8b8");
  }

  if (Ret < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: simple-framebuffer: properties: %a\n", FdtStrerror (Ret)));
    return;
  }

  DEBUG ((
    DEBUG_INFO,
    "FdtPlatform: simple-framebuffer %ux%u stride %u at 0x%lx\n",
    Width,
    Height,
    Stride,
    Base
    ));
}

/**
  Keep the USB-A ports powered under the OS.

  The mainline TaishanPi 3M tree names its two USB-A VBUS regulators only
  from usb-a-connector nodes, and no driver enables a connector's supply
  behind a hub, so Linux switched them off and lost the USB stick it was
  booting from.  The firmware's own tree carries regulator-always-on; a
  tree loaded from the boot partition may not, so the properties are set
  here on whichever tree is handed over.
**/
STATIC
VOID
FdtFixupUsbAVbus (
  IN VOID  *Fdt
  )
{
  STATIC CONST CHAR8  *Regulators[] = {
    "/regulator-vcc-5v0-usb2-host",
    "/regulator-vcc-5v0-usb3-host",
  };

  UINTN  Index;
  INT32  Node;
  INT32  Ret;

  if (!FixedPcdGetBool (PcdFdtUsbAVbusAlwaysOn)) {
    return;
  }

  for (Index = 0; Index < ARRAY_SIZE (Regulators); Index++) {
    Node = FdtPathOffset (Fdt, Regulators[Index]);
    if (Node < 0) {
      DEBUG ((DEBUG_WARN, "FdtPlatform: no %a in this tree, USB-A VBUS left as it is\n", Regulators[Index]));
      continue;
    }

    Ret = FdtSetPropEmpty (Fdt, Node, "regulator-always-on");
    if (Ret >= 0) {
      Ret = FdtSetPropEmpty (Fdt, Node, "regulator-boot-on");
    }

    if (Ret < 0) {
      DEBUG ((DEBUG_ERROR, "FdtPlatform: %a always-on: %a\n", Regulators[Index], FdtStrerror (Ret)));
    } else {
      DEBUG ((DEBUG_INFO, "FdtPlatform: %a kept on\n", Regulators[Index]));
    }
  }
}

/**
  The DSI host that has a panel@0 child, or a negative libfdt error.

  Looked up again after every change to the tree: a property that changes
  size moves every node behind it, so no offset survives a write.
**/
STATIC
INT32
FdtFindDsiWithPanel (
  IN  VOID   *Fdt,
  OUT INT32  *PanelNode
  )
{
  INT32  Dsi;

  for (Dsi = FdtNodeOffsetByCompatible (Fdt, -1, "rockchip,rk3576-mipi-dsi2");
       Dsi >= 0;
       Dsi = FdtNodeOffsetByCompatible (Fdt, Dsi, "rockchip,rk3576-mipi-dsi2"))
  {
    *PanelNode = FdtSubnodeOffset (Fdt, Dsi, "panel@0");
    if (*PanelNode >= 0) {
      return Dsi;
    }
  }

  return -FDT_ERR_NOTFOUND;
}

/**
  The first phandle in a property, or 0.
**/
STATIC
UINT32
FdtGetFirstPhandle (
  IN VOID         *Fdt,
  IN INT32        Node,
  IN CONST CHAR8  *Property
  )
{
  CONST UINT32  *Cells;
  INT32         Length;

  Cells = FdtGetProp (Fdt, Node, Property, &Length);
  if ((Cells == NULL) || (Length < (INT32)sizeof (UINT32))) {
    return 0;
  }

  return Fdt32ToCpu (Cells[0]);
}

STATIC
INT32
FdtSetPhandleStatus (
  IN VOID     *Fdt,
  IN UINT32   Phandle,
  IN BOOLEAN  Enable
  )
{
  INT32  Node;

  Node = FdtNodeOffsetByPhandle (Fdt, Phandle);
  if (Node < 0) {
    return Node;
  }

  return FdtSetPropString (Fdt, Node, "status", Enable ? "okay" : "disabled");
}

/**
  The MIPI DSI screen, if the board has one and it is attached.

  The board's device tree describes the panel under the DSI host
  (panel@0) and its backlight, written disabled, because whether the screen
  is there is only known once the firmware has asked it: the board's
  ROCKCHIP_DSI_PANEL_PROTOCOL Detect.  So this runs at ReadyToBoot, after
  the consoles are connected, on the firmware's own tree.  A tree loaded
  from the boot partition is the user's and is not changed.

  - The backlight and its PWM channel: on when the screen is attached and
    either the firmware lit it (the OS keeps that picture) or the OS will
    drive the screen natively.  With force GOP on and the screen not lit
    (HDMI was the screen), a backlight with nothing behind it stays off.
    Its default-brightness-level becomes the level the firmware used, so
    pwm-backlight starts where the firmware left it.
  - The DSI host and its D-PHY: on only when the screen is attached and
    force GOP is off.  With force GOP on, the OS shows the firmware's
    framebuffer and FdtFixupVopDevices has disabled the VOP under it; a
    panel driver probing would reset the panel.

  Boards without the protocol, or whose tree has no panel@0 under a DSI
  host, are left alone.
**/
STATIC
VOID
FdtFixupDsiPanel (
  IN VOID  *Fdt
  )
{
  EFI_STATUS                   Status;
  ROCKCHIP_DSI_PANEL_PROTOCOL  *Panel;
  BOOLEAN                      Attached;
  BOOLEAN                      Native;
  BOOLEAN                      Backlight;
  INT32                        Dsi;
  INT32                        PanelNode;
  INT32                        Node;
  UINT32                       PhyPhandle;
  UINT32                       BacklightPhandle;
  UINT32                       PwmPhandle;
  CONST UINT32                 *Cells;
  INT32                        Length;
  UINT32                       Percent;
  UINT32                       Min;
  UINT32                       Max;
  UINT32                       Steps;
  UINT32                       Target;
  UINT32                       Level;
  INT32                        Ret;

  Status = gBS->LocateProtocol (&gRockchipDsiPanelProtocolGuid, NULL, (VOID **)&Panel);
  if (EFI_ERROR (Status)) {
    return;
  }

  Dsi = FdtFindDsiWithPanel (Fdt, &PanelNode);
  if (Dsi < 0) {
    DEBUG ((DEBUG_INFO, "FdtPlatform: DSI panel: none described in this tree\n"));
    return;
  }

  PhyPhandle       = FdtGetFirstPhandle (Fdt, Dsi, "phys");
  BacklightPhandle = FdtGetFirstPhandle (Fdt, PanelNode, "backlight");
  PwmPhandle       = 0;
  if (BacklightPhandle != 0) {
    Node = FdtNodeOffsetByPhandle (Fdt, BacklightPhandle);
    if (Node >= 0) {
      PwmPhandle = FdtGetFirstPhandle (Fdt, Node, "pwms");
    }
  }

  Attached  = (Panel->Detect == NULL) || !EFI_ERROR (Panel->Detect (Panel));
  Native    = !PcdGet8 (PcdFdtForceGop);
  Backlight = Attached && (Native || (Panel->BacklightPercent != 0));

  //
  // The DSI host and the D-PHY in its "phys".
  //
  Ret = FdtSetPropString (Fdt, Dsi, "status", (Attached && Native) ? "okay" : "disabled");
  if ((Ret >= 0) && (PhyPhandle != 0)) {
    Ret = FdtSetPhandleStatus (Fdt, PhyPhandle, Attached && Native);
  }

  if (Ret < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: DSI panel: DSI host status: %a\n", FdtStrerror (Ret)));
  }

  if (BacklightPhandle == 0) {
    DEBUG ((DEBUG_INFO, "FdtPlatform: DSI panel: attached=%u native=%u, no backlight\n", Attached, Native));
    return;
  }

  //
  // The backlight the panel names, and the PWM channel in its "pwms".
  //
  Ret = FdtSetPhandleStatus (Fdt, BacklightPhandle, Backlight);
  if ((Ret >= 0) && (PwmPhandle != 0)) {
    Ret = FdtSetPhandleStatus (Fdt, PwmPhandle, Backlight);
  }

  if (Ret < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: DSI panel: backlight status: %a\n", FdtStrerror (Ret)));
    return;
  }

  //
  // Start at the firmware's level.  Only the two-entry interpolated form,
  // brightness-levels = <min max> with num-interpolated-steps = <n>, is
  // understood: level i is min + i * (max - min) / n, out of max.  Any
  // other table keeps its default.
  //
  Percent = Panel->BacklightPercent;
  if (Percent == 0) {
    Percent = PcdGet8 (PcdDisplayBrightness);
  }

  Percent = MIN (MAX (Percent, 1), 100);

  Node = FdtNodeOffsetByPhandle (Fdt, BacklightPhandle);
  if (Node < 0) {
    return;
  }

  Cells = FdtGetProp (Fdt, Node, "brightness-levels", &Length);
  if ((Cells == NULL) || (Length != 2 * sizeof (UINT32))) {
    return;
  }

  Min   = Fdt32ToCpu (Cells[0]);
  Max   = Fdt32ToCpu (Cells[1]);
  Cells = FdtGetProp (Fdt, Node, "num-interpolated-steps", &Length);
  if ((Cells == NULL) || (Length != sizeof (UINT32)) || (Max <= Min)) {
    return;
  }

  Steps  = Fdt32ToCpu (Cells[0]);
  Target = (Max * Percent + 50) / 100;
  if (Target <= Min) {
    Level = 0;
  } else {
    Level = MIN (((Target - Min) * Steps + (Max - Min) / 2) / (Max - Min), Steps);
  }

  DEBUG ((
    DEBUG_INFO,
    "FdtPlatform: DSI panel: attached=%u native=%u, backlight %a at %u%% (level %u of %u)\n",
    Attached,
    Native,
    Backlight ? "on" : "off",
    Percent,
    Level,
    Steps
    ));

  Level = CpuToFdt32 (Level);
  Ret   = FdtSetProp (Fdt, Node, "default-brightness-level", &Level, sizeof (Level));
  if (Ret < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: DSI panel: default-brightness-level: %a\n", FdtStrerror (Ret)));
  }
}

STATIC
EFI_STATUS
EFIAPI
ApplyPlatformFdtFixups (
  IN OUT VOID  **Fdt
  )
{
  EFI_STATUS  Status;

  // Expand the FDT enough for the ForceGop clock keep-alive nodes + bootargs.
  Status = FdtOpenIntoAlloc (Fdt, NULL, FdtTotalSize (*Fdt) + SIZE_8KB);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  FdtFixupComboPhyDevices (*Fdt);
  FdtFixupPcieResources (*Fdt);
  FdtFixupVopDevices (*Fdt);
  FdtFixupNorFlashDevices (*Fdt);
  FdtFixupUfsDevices (*Fdt);
  FdtFixupUsbDrd0 (*Fdt);
  FdtFixupUsbAVbus (*Fdt);
  FdtFixupChosen (*Fdt);
  FdtFixupSimpleFramebuffer (*Fdt);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ReadFdtFromFilePath (
  IN      EFI_FILE_PROTOCOL  *Root,
  IN      CHAR16             *Path,
  IN OUT  UINTN              *FileSize  OPTIONAL,
  IN OUT  VOID               **Fdt
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File;
  UINTN              FileBufferSize;
  UINTN              FileInfoSize;
  EFI_FILE_INFO      *FileInfo = NULL;
  INT32              Ret;

  Status = Root->Open (Root, &File, Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      Status == EFI_NOT_FOUND ? DEBUG_VERBOSE : DEBUG_ERROR,
      "FdtPlatform: Couldn't open '%s'. Status=%r\n",
      Path,
      Status
      ));
    return Status;
  }

  if (FileSize == NULL) {
    FileInfoSize = 0;
    Status       = File->GetInfo (File, &gEfiFileInfoGuid, &FileInfoSize, NULL);
    if (EFI_ERROR (Status) && (FileInfoSize == 0)) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Failed to get '%s' file info size. Status=%r\n",
        Path,
        Status
        ));
      goto Exit;
    }

    FileInfo = AllocatePool (FileInfoSize);
    if (FileInfo == NULL) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Not enough resources for '%s' file info (%d bytes).\n",
        Path,
        FileInfoSize
        ));
      Status = EFI_OUT_OF_RESOURCES;
      goto Exit;
    }

    Status = File->GetInfo (File, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Failed to get '%s' file info. Status=%r\n",
        Path,
        Status
        ));
      goto Exit;
    }

    FileBufferSize = FileInfo->FileSize;
  } else {
    FileBufferSize = *FileSize;
  }

  *Fdt = AllocatePool (FileBufferSize);
  if (*Fdt == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "FdtPlatform: Not enough resources for '%s' file buffer (%d bytes).\n",
      Path,
      FileBufferSize
      ));
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  Status = File->Read (File, &FileBufferSize, *Fdt);
  if (EFI_ERROR (Status)) {
    if (FileSize != NULL) {
      *FileSize = FileBufferSize;
    }

    DEBUG ((
      DEBUG_ERROR,
      "FdtPlatform: Failed to read '%s' (%d bytes). Status=%r\n",
      Path,
      FileBufferSize,
      Status
      ));
    goto Exit;
  }

  Ret = FdtCheckHeader (*Fdt);
  if (Ret) {
    DEBUG ((
      DEBUG_ERROR,
      "FdtPlatform: '%s' has an invalid header! Ret=%a\n",
      Path,
      FdtStrerror (Ret)
      ));
    Status = EFI_LOAD_ERROR;
    goto Exit;
  }

Exit:
  Root->Close (File);
  if (FileInfo != NULL) {
    FreePool (FileInfo);
  }

  if (EFI_ERROR (Status) && (*Fdt != NULL)) {
    FreePool (*Fdt);
    *Fdt = NULL;
  }

  return Status;
}

//
// Assume the FDT ends with a strings block.
// See https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html
//
#define FdtUsedSize(Fdt)  (FdtOffsetDtStrings (Fdt) + FdtSizeDtStrings (Fdt))

STATIC
EFI_STATUS
EFIAPI
InstallOverlaysFromDirectoryPath (
  IN      EFI_FILE_PROTOCOL  *Root,
  IN      CHAR16             *Path,
  IN OUT  VOID               **Fdt,
  IN OUT  UINTN              *OverlaysCount
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Dir;
  UINTN              DirEntryInfoSize;
  UINTN              CurrentInfoSize;
  EFI_FILE_INFO      *DirEntryInfo;
  VOID               *FdtOverlay;
  INT32              Ret;

  Status = Root->Open (Root, &Dir, Path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      Status == EFI_NOT_FOUND ? DEBUG_VERBOSE : DEBUG_ERROR,
      "FdtPlatform: Couldn't open directory '%s'. Status=%r\n",
      Path,
      Status
      ));
    return Status;
  }

  DirEntryInfoSize = sizeof (EFI_FILE_INFO) + MAX_PATH_LENGTH;
  DirEntryInfo     = AllocatePool (DirEntryInfoSize);
  if (DirEntryInfo == NULL) {
    DEBUG ((
      DEBUG_ERROR,
      "FdtPlatform: Not enough resources for '%s' directory entry info.\n",
      Path
      ));
    Root->Close (Dir);
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((DEBUG_INFO, "FdtPlatform: Processing overlays in sub-directory '%s'\n", Path));

  while (TRUE) {
    CurrentInfoSize = DirEntryInfoSize;
    Status          = Dir->Read (Dir, &CurrentInfoSize, (VOID *)DirEntryInfo);
    if (EFI_ERROR (Status) || (CurrentInfoSize == 0)) {
      // Reached end of directory?
      ASSERT (CurrentInfoSize <= DirEntryInfoSize);
      ASSERT_EFI_ERROR (Status);
      break;
    }

    if (DirEntryInfo->Attribute & EFI_FILE_DIRECTORY) {
      continue;
    }

    if (!StrEndsWith (DirEntryInfo->FileName, L".dtbo")) {
      continue;
    }

    DEBUG ((
      DEBUG_INFO,
      "FdtPlatform: Installing overlay '%s'\n",
      DirEntryInfo->FileName
      ));

    Status = ReadFdtFromFilePath (
               Dir,
               DirEntryInfo->FileName,
               &DirEntryInfo->FileSize,
               &FdtOverlay
               );
    if (EFI_ERROR (Status)) {
      if (Status == EFI_OUT_OF_RESOURCES) {
        break;
      }

      continue;
    }

    if (FdtUsedSize (*Fdt) + DirEntryInfo->FileSize >= FdtTotalSize (*Fdt)) {
      //
      // Expand the buffer by at least 8 KB, so we don't end up
      // reallocating for every small overlay.
      //
      Status = FdtOpenIntoAlloc (
                 Fdt,
                 NULL,
                 FdtTotalSize (*Fdt) + MAX (DirEntryInfo->FileSize, SIZE_8KB)
                 );
      if (EFI_ERROR (Status)) {
        FreePool (FdtOverlay);
        break;
      }
    }

    Ret = FdtOverlayApply (*Fdt, FdtOverlay);
    FreePool (FdtOverlay);
    if (Ret) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Failed to apply overlay '%s' (%d bytes). Ret=%a\n",
        DirEntryInfo->FileName,
        DirEntryInfo->FileSize,
        FdtStrerror (Ret)
        ));

      if (Ret == -FDT_ERR_NOSPACE) {
        DEBUG ((
          DEBUG_ERROR,
          "FdtPlatform:   FDT bytes used: %d, total: %d\n",
          FdtUsedSize (*Fdt),
          FdtTotalSize (*Fdt)
          ));
      }

      //
      // The FDT is damaged at this point, we can't continue.
      //
      Status = EFI_LOAD_ERROR;
      break;
    }

    *OverlaysCount += 1;
  }

  FreePool (DirEntryInfo);
  Root->Close (Dir);

  return Status;
}

STATIC CHAR16  *mDtbOverrideBasePaths[] = {
  NULL,
  L"\\dtb",
  L"\\dtb\\base",
  L"\\dtb\\rockchip",
};

STATIC CHAR16  *mDtbOverrideOverlayPaths[] = {
  NULL,
  L"\\dtb\\overlays",
  L"\\dtb\\overlays\\!",
};

STATIC
EFI_STATUS
FdtPlatformBuildOverridePaths (
  VOID
  )
{
  UINTN        Index;
  CONST CHAR8  *FdtName;
  UINTN        FdtNameLen;
  CHAR16       *OriginalPath;
  CHAR16       *Path;
  UINTN        PathSize;
  UINTN        BasePathLen;

  FdtName    = (CONST CHAR8 *)PcdGetPtr (PcdDeviceTreeName);
  FdtNameLen = AsciiStrLen (FdtName);

  //
  // Build base FDT override file paths.
  // If a directory is provided, the platform FDT file name
  // will be appended to it.
  //
  mDtbOverrideBasePaths[0] = (CHAR16 *)PcdGetPtr (PcdFdtOverrideBasePath);

  for (Index = 0; Index < ARRAY_SIZE (mDtbOverrideBasePaths); Index++) {
    OriginalPath = mDtbOverrideBasePaths[Index];
    if (OriginalPath == NULL) {
      continue;
    } else if (OriginalPath[0] == CHAR_NULL) {
      mDtbOverrideBasePaths[Index] = NULL;
      continue;
    }

    if (!StrEndsWith (OriginalPath, L".dtb")) {
      BasePathLen = StrLen (OriginalPath);
      PathSize    = sizeof (CHAR16) *
                    (BasePathLen +
                     StrLen (L"\\") +
                     FdtNameLen +
                     StrLen (L".dtb") + 1);

      Path = AllocatePool (PathSize);
      if (Path == NULL) {
        ASSERT (FALSE);
        return EFI_OUT_OF_RESOURCES;
      }

      UnicodeSPrint (Path, PathSize, L"%s\\%a.dtb", OriginalPath, FdtName);

      mDtbOverrideBasePaths[Index] = Path;
    }
  }

  //
  // Build FDT overlay directory paths.
  // If ending in "!", the platform FDT base name will be appended.
  //
  mDtbOverrideOverlayPaths[0] = (CHAR16 *)PcdGetPtr (PcdFdtOverrideOverlayPath);

  for (Index = 0; Index < ARRAY_SIZE (mDtbOverrideOverlayPaths); Index++) {
    OriginalPath = mDtbOverrideOverlayPaths[Index];
    if (OriginalPath == NULL) {
      continue;
    } else if (OriginalPath[0] == CHAR_NULL) {
      mDtbOverrideOverlayPaths[Index] = NULL;
      continue;
    }

    if (StrEndsWith (OriginalPath, L"!")) {
      BasePathLen = StrLen (OriginalPath) - StrLen (L"!");
      PathSize    = sizeof (CHAR16) *
                    (BasePathLen +
                     FdtNameLen + 1);

      Path = AllocatePool (PathSize);
      if (Path == NULL) {
        ASSERT (FALSE);
        return EFI_OUT_OF_RESOURCES;
      }

      UnicodeSPrint (Path, PathSize, L"%.*s%a", BasePathLen, OriginalPath, FdtName);

      mDtbOverrideOverlayPaths[Index] = Path;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FdtPlatformProcessFileSystem (
  IN  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *Root;
  UINTN              Index;
  CHAR16             *Path;
  VOID               *Fdt          = NULL;
  VOID               *NewFdt       = NULL;
  VOID               *FdtToInstall = NULL;
  UINTN              OverlaysCount = 0;
  INT32              Ret;

  Status = FileSystem->OpenVolume (FileSystem, &Root);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: Failed to open volume. Status=%r\n", Status));
    return Status;
  }

  //
  // Look for a base FDT override.
  //
  for (Index = 0; Index < ARRAY_SIZE (mDtbOverrideBasePaths); Index++) {
    Path = mDtbOverrideBasePaths[Index];
    if (Path == NULL) {
      continue;
    }

    Status = ReadFdtFromFilePath (Root, Path, NULL, &Fdt);
    if (!EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "FdtPlatform: Loaded FDT override '%s'.\n", Path));
      break;
    }
  }

  if (Fdt == NULL) {
    if (mPlatformFdt == NULL) {
      return Status;
    }

    // Not found - use the platform FDT instead.
    Fdt = mPlatformFdt;
  }

  //
  // Clone the FDT so that we can restore the original one
  // in case it gets damaged.
  //
  Status = FdtOpenIntoAlloc (&Fdt, &NewFdt, FdtTotalSize (Fdt));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Apply platform fix-ups to the base FDT override.
  //
  if (PcdGet8 (PcdFdtOverrideFixup) && (Fdt != mPlatformFdt)) {
    ApplyPlatformFdtFixups (&NewFdt);
  }

  //
  // Process the overlays.
  //
  for (Index = 0; Index < ARRAY_SIZE (mDtbOverrideOverlayPaths); Index++) {
    Path = mDtbOverrideOverlayPaths[Index];
    if (Path == NULL) {
      continue;
    }

    Status = InstallOverlaysFromDirectoryPath (Root, Path, &NewFdt, &OverlaysCount);
    if ((Status == EFI_LOAD_ERROR) || (Status == EFI_OUT_OF_RESOURCES)) {
      break;
    } else {
      // Ignore non-fatal errors.
      Status = EFI_SUCCESS;
    }
  }

  //
  // Use the new FDT if it overrides the platform default and/or has
  // overlays installed.
  //
  if (!EFI_ERROR (Status)) {
    Ret = FdtCheckHeader (NewFdt);
    if (Ret == 0) {
      if ((Fdt != mPlatformFdt) || (OverlaysCount > 0)) {
        FdtToInstall = NewFdt;
        DEBUG ((DEBUG_INFO, "FdtPlatform: Using FDT with %d overlays merged.\n", OverlaysCount));
      }
    } else {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: New FDT has an invalid header! Ret=%a\n",
        FdtStrerror (Ret)
        ));
    }
  }

  if (FdtToInstall != NewFdt) {
    FreePool (NewFdt);
  }

  //
  // In case the new FDT was unsuitable, fall back to the original if that
  // still overrides the platform default.
  //
  if (Fdt != mPlatformFdt) {
    if (FdtToInstall == NULL) {
      FdtToInstall = Fdt;
      DEBUG ((DEBUG_INFO, "FdtPlatform: Using original FDT without overlays.\n"));
    } else {
      FreePool (Fdt);
    }
  }

  if (FdtToInstall != NULL) {
    Status = gBS->InstallConfigurationTable (&gFdtTableGuid, FdtToInstall);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Failed to install the new FDT as config table. Status=%r\n",
        Status
        ));

      if (FdtToInstall != mPlatformFdt) {
        FreePool (FdtToInstall);
      }
    }
  } else if (!EFI_ERROR (Status)) {
    Status = EFI_NOT_FOUND;
  }

  return Status;
}

STATIC
UINTN
GetParentDevicePathSize (
  IN CONST EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  CONST EFI_DEVICE_PATH_PROTOCOL  *Node;

  if ((DevicePath == NULL) || IsDevicePathEnd (DevicePath)) {
    return 0;
  }

  Node = DevicePath;

  while (!IsDevicePathEnd (NextDevicePathNode (Node))) {
    Node = NextDevicePathNode (Node);
  }

  return (UINTN)((UINT8 *)Node - (UINT8 *)DevicePath);
}

STATIC
EFI_STATUS
FdtPlatformProcessLoadedImage (
  IN EFI_LOADED_IMAGE_PROTOCOL  *LoadedImage
  )
{
  EFI_STATUS                       Status;
  EFI_DEVICE_PATH_PROTOCOL         *ImageDevicePath;
  UINTN                            DevicePathSize;
  EFI_DEVICE_PATH_PROTOCOL         *DevicePath;
  CHAR16                           *DevicePathText;
  EFI_HANDLE                       *Handles;
  UINTN                            HandleCount;
  UINTN                            Index;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem;

  if ((LoadedImage == NULL) || (LoadedImage->DeviceHandle == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  ImageDevicePath = DevicePathFromHandle (LoadedImage->DeviceHandle);
  if (ImageDevicePath == NULL) {
    return EFI_NOT_FOUND;
  }

  DevicePathSize = GetParentDevicePathSize (ImageDevicePath);
  if (DevicePathSize == 0) {
    DevicePathSize = GetDevicePathSize (ImageDevicePath);
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EFI_NOT_FOUND;

  for (Index = 0; Index < HandleCount; Index++) {
    DevicePath = DevicePathFromHandle (Handles[Index]);
    if (DevicePath == NULL) {
      continue;
    }

    if (CompareMem (DevicePath, ImageDevicePath, DevicePathSize) != 0) {
      continue;
    }

    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiSimpleFileSystemProtocolGuid,
                    (VOID **)&FileSystem
                    );
    if (EFI_ERROR (Status)) {
      ASSERT_EFI_ERROR (Status);
      continue;
    }

    DevicePathText = ConvertDevicePathToText (DevicePath, FALSE, FALSE);
    DEBUG ((DEBUG_INFO, "FdtPlatform: Processing '%s'\n", DevicePathText));
    if (DevicePathText != NULL) {
      FreePool (DevicePathText);
    }

    Status = FdtPlatformProcessFileSystem (FileSystem);
    if (EFI_ERROR (Status)) {
      if (Status != EFI_NOT_FOUND) {
        DEBUG ((DEBUG_ERROR, "FdtPlatform: Failed to process the file system. Status=%r\n", Status));
      }

      continue;
    }

    break;
  }

  FreePool (Handles);

  return Status;
}

STATIC
VOID
EFIAPI
NotifyLoadedImage (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                 Status;
  EFI_HANDLE                 *Handles;
  UINTN                      HandleCount;
  EFI_LOADED_IMAGE_PROTOCOL  *LoadedImage;

  while (TRUE) {
    Status = gBS->LocateHandleBuffer (
                    ByRegisterNotify,
                    NULL,
                    mLoadedImageEventRegistration,
                    &HandleCount,
                    &Handles
                    );
    if (EFI_ERROR (Status)) {
      if (Status != EFI_NOT_FOUND) {
        DEBUG ((
          DEBUG_ERROR,
          "FdtPlatform: Failed to locate gEfiLoadedImageProtocolGuid. Status=%r\n",
          Status
          ));
      }

      break;
    }

    ASSERT (HandleCount == 1);

    Status = gBS->HandleProtocol (
                    Handles[0],
                    &gEfiLoadedImageProtocolGuid,
                    (VOID **)&LoadedImage
                    );
    FreePool (Handles);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "FdtPlatform: Failed to get gEfiLoadedImageProtocolGuid. Status=%r\n",
        Status
        ));
      break;
    }

    Status = FdtPlatformProcessLoadedImage (LoadedImage);
    if (EFI_ERROR (Status)) {
      continue;
    }
  }
}

STATIC
VOID
EFIAPI
NotifyReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;
  VOID        *Dtb;

  gBS->CloseEvent (Event);

  //
  // The screen has a mode by now, which it did not have when the tree was
  // installed.
  //
  if (!EFI_ERROR (EfiGetSystemConfigurationTable (&gFdtTableGuid, &Dtb)) && (Dtb != NULL)) {
    FdtFixupSimpleFramebuffer (Dtb);

    //
    // Only the firmware's own tree: overrides are installed after this, from
    // NotifyLoadedImage, and are left as the user wrote them.
    //
    if (Dtb == mPlatformFdt) {
      FdtFixupDsiPanel (Dtb);
    }
  }

  if (!PcdGet8 (PcdFdtSupportOverrides)) {
    return;
  }

  Status = FdtPlatformBuildOverridePaths ();
  if (EFI_ERROR (Status)) {
    ASSERT_EFI_ERROR (Status);
    return;
  }

  EfiCreateProtocolNotifyEvent (
    &gEfiLoadedImageProtocolGuid,
    TPL_CALLBACK,
    NotifyLoadedImage,
    NULL,
    &mLoadedImageEventRegistration
    );
}

STATIC
VOID
EFIAPI
NotifyExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;
  VOID        *Dtb;

  Status = EfiGetSystemConfigurationTable (&gFdtTableGuid, &Dtb);
  if (EFI_ERROR (Status) || (Dtb == NULL)) {
    DEBUG ((DEBUG_WARN, "FdtPlatform: No FDT installed!\n"));
  } else if (FdtTotalSize (Dtb) <= SIZE_4KB) {
    // Some loaders may install a dummy table, warn in this case too.
    DEBUG ((DEBUG_WARN, "FdtPlatform: No usable FDT installed!\n"));
  }
}

STATIC
EFI_STATUS
EFIAPI
LoadPlatformFdt (
  OUT   VOID    **PlatformFdt,
  IN    UINT32  CompatMode
  )
{
  EFI_STATUS      Status;
  CONST EFI_GUID  *DtbFileGuid;
  VOID            *Fdt;
  UINTN           FdtSize;
  INT32           Ret;

  switch (CompatMode) {
    case FDT_COMPAT_MODE_VENDOR:
      DEBUG ((DEBUG_INFO, "FdtPlatform: Using vendor FDT.\n"));
      break;
    case FDT_COMPAT_MODE_MAINLINE:
      DEBUG ((DEBUG_INFO, "FdtPlatform: Using mainline FDT.\n"));
      break;
    case FDT_COMPAT_MODE_UNSUPPORTED:
      DEBUG ((DEBUG_WARN, "FdtPlatform: No firmware FDT found!\n"));
      return EFI_NOT_FOUND;
    default:
      DEBUG ((DEBUG_ERROR, "FdtPlatform: Invalid compat mode: %u\n", CompatMode));
      ASSERT (FALSE);
      return EFI_INVALID_PARAMETER;
  }

  DtbFileGuid = PlatformGetDtbFileGuid (CompatMode);
  if (DtbFileGuid == NULL) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: Compat mode unsupported by platform!\n"));
    ASSERT (FALSE);
    return EFI_UNSUPPORTED;
  }

  Status = GetSectionFromAnyFv (
             DtbFileGuid,
             EFI_SECTION_RAW,
             0,
             &Fdt,
             &FdtSize
             );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  Ret = FdtCheckHeader (Fdt);
  if (Ret) {
    DEBUG ((
      DEBUG_ERROR,
      "FdtPlatform: Firmware FDT has an invalid header! Ret=%a\n",
      FdtStrerror (Ret)
      ));
    return EFI_NOT_FOUND;
  }

  *PlatformFdt = Fdt;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
FdtPlatformDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;
  UINT32      CompatMode;

  if ((PcdGet32 (PcdConfigTableMode) & CONFIG_TABLE_MODE_FDT) == 0) {
    DEBUG ((DEBUG_WARN, "FdtPlatform: FDT support is disabled by the settings.\n"));
    return EFI_UNSUPPORTED;
  }

  //
  // Always: FdtFixupDsiPanel needs the connected consoles as well, and a
  // board without a DSI panel returns from it at once.
  //
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,                  // Type
                  TPL_CALLBACK,                       // NotifyTpl
                  NotifyReadyToBoot,                  // NotifyFunction
                  NULL,                               // NotifyContext
                  &gEfiEventReadyToBootGuid,          // EventGroup
                  &Event                              // Event
                  );
  ASSERT_EFI_ERROR (Status);

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,                  // Type
                  TPL_CALLBACK,                       // NotifyTpl
                  NotifyExitBootServices,             // NotifyFunction
                  NULL,                               // NotifyContext
                  &gEfiEventExitBootServicesGuid,     // EventGroup
                  &Event                              // Event
                  );
  ASSERT_EFI_ERROR (Status);

  CompatMode = PcdGet32 (PcdFdtCompatMode);

  Status = LoadPlatformFdt (&mPlatformFdt, CompatMode);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "FdtPlatform: Failed to load firmware FDT. Status=%r\n", Status));
    return EFI_SUCCESS;
  }

  ApplyPlatformFdtFixups (&mPlatformFdt);

  Status = gBS->InstallConfigurationTable (&gFdtTableGuid, mPlatformFdt);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_WARN,
      "FdtPlatform: Failed to install firmware DTB as config table. Status=%r\n",
      Status
      ));
  }

  return EFI_SUCCESS;
}

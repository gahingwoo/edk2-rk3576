/** @file
 *
 *  Flattened Device Tree platform driver
 *
 *  Copyright (c) 2023-2025, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Protocol/LoadedImage.h>
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
FdtFixupPowerDomains (
  IN VOID  *Fdt
  )
{
  INT32  Node;
  INT32  Ret;

  //
  // PD_USB (RK3576_PD_USB = 7) is a child of PD_VOP (RK3576_PD_VOP = 18) in
  // the hardware PMU hierarchy.  When PD_VOP is powered off (it has no VOP/HDMI
  // consumers because we disabled those nodes for force-GOP mode), the RK3576
  // PMU physically cuts power to the entire PD_VOP subtree — including PD_USB.
  //
  // Even though DWC3 (usb@23000000) holds a software genpd reference to
  // PD_USB, the hardware-level power gate on PD_VOP makes the DWC3 MMIO
  // inaccessible.  Any register read (e.g. xhci_portsc_readl during
  // hub_suspend) causes an AXI slave error (ESR 0xbf000002) → kernel panic.
  //
  // Fix: mark both PD_VOP and PD_USB as "always-on" in the FDT.  The
  // rockchip-pm-domain driver respects this DT property by setting
  // GENPD_FLAG_ALWAYS_ON on the genpd object, preventing genpd from ever
  // powering off these domains.  PD_VOP stays powered (keeping PD_USB
  // accessible), and DWC3 operates without hardware power being cut under it.
  //

  DEBUG ((DEBUG_INFO, "FdtPlatform: Marking PD_VOP and PD_USB as always-on\n"));

  //
  // PD_VOP: /soc/power-management@27380000/power-controller/power-domain@18
  //
  Node = FdtPathOffset (Fdt,
           "/soc/power-management@27380000/power-controller"
           "/power-domain@" XS (RK3576_PD_VOP));
  if (Node < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: PD_VOP node not found: %a\n", FdtStrerror (Node)));
  } else {
    Ret = FdtSetPropEmpty (Fdt, Node, "always-on");
    if (Ret < 0) {
      DEBUG ((DEBUG_ERROR, "FdtPlatform: Failed to set PD_VOP always-on: %a\n", FdtStrerror (Ret)));
    }
  }

  //
  // PD_USB: .../power-domain@18/power-domain@7
  //
  Node = FdtPathOffset (Fdt,
           "/soc/power-management@27380000/power-controller"
           "/power-domain@" XS (RK3576_PD_VOP)
           "/power-domain@" XS (RK3576_PD_USB));
  if (Node < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: PD_USB node not found: %a\n", FdtStrerror (Node)));
  } else {
    Ret = FdtSetPropEmpty (Fdt, Node, "always-on");
    if (Ret < 0) {
      DEBUG ((DEBUG_ERROR, "FdtPlatform: Failed to set PD_USB always-on: %a\n", FdtStrerror (Ret)));
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
  INT32       Node;
  INT32       Ret;
  CONST VOID  *Phys;
  INT32       PhysLen;
  UINT32      U2PhyPhandle;

  //
  // The Samsung USBDP combo PHY (usbdp_phy @ 0x2B010000) for DRD0 (USB-C) is
  // not yet supported by the mainline kernel.
  //
  // Simply disabling the usbdp_phy DT node does NOT fix the problem: the DWC3
  // node (usb@23000000) still has a phandle reference to it in its "phys"
  // property.  When the kernel PHY subsystem cannot find a registered provider
  // for that phandle (because the node is disabled and its driver never probed),
  // it returns -EPROBE_DEFER rather than -ENODEV.  DWC3 then defers its probe
  // for the full 30-second deferred_probe_timeout and ultimately fails with
  // -ETIMEDOUT, leaving the USB-C port completely dead.
  //
  // Correct fix: REMOVE the USB3 PHY phandle from the "phys" and "phy-names"
  // properties of usb@23000000 so that DWC3 never asks for it.  DWC3 will then
  // probe successfully in USB2-only (HS) mode, which is sufficient to enumerate
  // mass-storage devices.
  //
  // The "phys" property layout (before this fixup):
  //   [u2phy0_otg phandle (1 cell, #phy-cells=0)]
  //   [usbdp_phy phandle  (1 cell)]
  //   [PHY_TYPE_USB3      (1 cell, the usbdp_phy specifier, #phy-cells=1)]
  //   Total = 12 bytes
  //
  // We keep only the first 4 bytes (u2phy0_otg phandle) and rewrite
  // phy-names as "usb2-phy".
  //
  // Also disable the usbdp_phy node itself so the kernel does not attempt
  // (and fail) to bind the unsupported driver.
  //

  DEBUG ((DEBUG_INFO, "FdtPlatform: Trimming DRD0 phys to USB2-only\n"));

  Node = FdtPathOffset (Fdt, "/soc/usb@23000000");
  if (Node < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: usb@23000000 not found: %a\n", FdtStrerror (Node)));
    goto DisablePhy;
  }

  Phys = FdtGetProp (Fdt, Node, "phys", &PhysLen);
  if ((Phys == NULL) || (PhysLen < 4)) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: usb@23000000 phys property missing or too short\n"));
    goto DisablePhy;
  }

  //
  // Copy the first phandle (u2phy0_otg) before modifying the FDT, because
  // FdtSetProp may reallocate the internal data and invalidate the pointer.
  //
  CopyMem (&U2PhyPhandle, Phys, sizeof (UINT32));

  Ret = FdtSetProp (Fdt, Node, "phys", &U2PhyPhandle, sizeof (UINT32));
  if (Ret < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: Failed to trim phys property: %a\n", FdtStrerror (Ret)));
    goto DisablePhy;
  }

  Ret = FdtSetPropString (Fdt, Node, "phy-names", "usb2-phy");
  if (Ret < 0) {
    DEBUG ((DEBUG_ERROR, "FdtPlatform: Failed to update phy-names property: %a\n", FdtStrerror (Ret)));
  }

DisablePhy:
  //
  // Disable the usbdp_phy node regardless so the kernel does not probe the
  // unsupported Samsung USBDP PHY driver.
  //
  FdtEnableNode (Fdt, "/soc/phy@2b010000", FALSE);

  //
  // Remove power-domains from usb@23000000.
  //
  // PD_USB is a hardware child of PD_VOP in the RK3576 PMU.  With VOP/HDMI DT
  // nodes disabled (force-GOP mode), genpd has no consumers for PD_VOP and
  // powers it off via "PM: genpd: Disabling unused power domains" at ~2s.
  // This takes PD_USB down at the hardware level regardless of genpd's view of
  // PD_USB.  Even after DWC3 probes and re-requests PD_USB, the power can be
  // cut again between probe completion and the first hub_event workqueue run,
  // causing an AXI slave SError (0xbf000002) in xhci_portsc_readl.
  //
  // Fix: remove the power-domains property from usb@23000000 entirely.
  // UEFI has already powered and configured DWC3.  Without a power-domains
  // reference, the kernel treats the hardware as always-on and never attempts
  // to gate the DWC3 MMIO through genpd.
  //
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
  FdtFixupPowerDomains (*Fdt);
  FdtFixupUfsDevices (*Fdt);
  FdtFixupUsbDrd0 (*Fdt);
  FdtFixupChosen (*Fdt);

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

  gBS->CloseEvent (Event);

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

  if (PcdGet8 (PcdFdtSupportOverrides)) {
    Status = gBS->CreateEventEx (
                    EVT_NOTIFY_SIGNAL,                // Type
                    TPL_CALLBACK,                     // NotifyTpl
                    NotifyReadyToBoot,                // NotifyFunction
                    NULL,                             // NotifyContext
                    &gEfiEventReadyToBootGuid,        // EventGroup
                    &Event                            // Event
                    );
    ASSERT_EFI_ERROR (Status);
  }

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

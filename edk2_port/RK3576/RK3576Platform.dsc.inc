## @file
#
#  RK3576 Platform DSC include — for board-level DSC files
#
#  Copyright (c) 2025, ROCK 4D RK3576 Port
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

!include Silicon/Rockchip/RK3576/RK3576Base.dsc.inc

[PcdsFixedAtBuild.common]
  # RK3576 peripheral MMIO region
  gArmTokenSpaceGuid.PcdArmPrimaryCore|0x000

  # GIC-400 (GICv2) — from rk3576.dtsi: interrupt-controller@2a701000
  #   reg = <0x2a701000 (GICD)>, <0x2a702000 (GICC)>,
  #         <0x2a704000 (GICH)>, <0x2a706000 (GICV)>
  gArmTokenSpaceGuid.PcdGicDistributorBase|0x2A701000
  gArmTokenSpaceGuid.PcdGicInterruptInterfaceBase|0x2A702000

/** @file
 *
 *  Copyright (c) 2024, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

//
// RK3576 has no ACPI SCMI device.
//
// Scmi.asl (inherited from RK3588) drives SCMI by writing a doorbell register
// at 0xfec60030 and polling a shared-memory mailbox.  RK3576 does not work
// that way: rk3576.dtsi declares `compatible = "arm,scmi-smc"` with
// `arm,smc-id = <0x82000010>` and no mailbox node at all, so the transport is
// an SMC call -- which ASL cannot issue.  The doorbell address is an RK3588
// one and decodes to nothing here.
//
// Nothing referenced the methods either: no other .asl in this tree calls
// CLRG, CLRS, CLCS, VDLG or VDLS.  So the device was a namespace object that
// could only ever fail, pointing two OperationRegions at addresses this SoC
// does not have.
//

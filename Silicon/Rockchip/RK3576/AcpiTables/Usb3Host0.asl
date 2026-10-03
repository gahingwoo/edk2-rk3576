/** @file
*  DWC3 XHCI controller #0 in host mode.
*
*  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "AcpiTables.h"

Device (XHC0) {
    Name (_HID, "PNP0D10")
    Name (_UID, Zero)
    Name (_CCA, Zero)
    // TEST ONLY (branch test/xhc0-hidden): hide XHC0 from the OS, to find out
    // whether the WinPE shutdown WHEA (SEA on a port-2 PORTSC read, PA
    // 0x23000000 in the abort record) comes from this controller.
    Name (_STA, Zero)

    Method (_CRS, 0x0, Serialized) {
        Name (RBUF, ResourceTemplate() {
            Memory32Fixed (ReadWrite, 0x23000000, 0x400000)
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 293 }
        })
        Return (RBUF)
    }   
}

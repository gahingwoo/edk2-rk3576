/** @file
 *
 *  Common ACPI table header for ArmSoM CM5 on RPi CM4 IO Board (RK3576).
 *  This file is included by Dsdt.asl and all platform ASL sources.
 *
 *  Hardware configuration: see CM5RpiCM4IO.dsc for full details.
 *
 *  Copyright (c) 2019-2021, ARM Limited. All rights reserved.
 *  Copyright (c) Microsoft Corporation. All rights reserved.
 *  Copyright (c) 2025, ArmSoM CM5 EDK2 Port
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef ACPITABLES_H_
#define ACPITABLES_H_

// Include common RK3576 ACPI definitions
// (interrupt mapping, power domain tokens, DWC3 base addresses, etc.)
#include <Silicon/Rockchip/RK3576/AcpiTables/AcpiTables.h>

#endif // ACPITABLES_H_

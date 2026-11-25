/** @file
 *
 *  The hardware watchdog (WDT_NS) for an ACPI OS.  See Watchdog.c.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef RK3576_ACPI_WATCHDOG_H_
#define RK3576_ACPI_WATCHDOG_H_

#include <IndustryStandard/Acpi.h>

/**
  Prepare WDT_NS and fill in the WDAT before it is installed.

  @param[in,out]  AcpiHeader  The WDAT as read from the firmware volume.

  @return  TRUE to install the table, FALSE to drop it.
**/
BOOLEAN
WatchdogPrepareWdat (
  IN OUT EFI_ACPI_DESCRIPTION_HEADER  *AcpiHeader
  );

#endif // RK3576_ACPI_WATCHDOG_H_

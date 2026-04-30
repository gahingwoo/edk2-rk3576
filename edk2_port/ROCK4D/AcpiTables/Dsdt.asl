// SPDX-License-Identifier: BSD-2-Clause-Patent
// Minimal DSDT for Radxa ROCK 4D (RK3576)
// FDT mode is primary for Linux BSP — ACPI tables are stubs.

DefinitionBlock ("Dsdt.aml", "DSDT", 2, "RKCP  ", "RK3576  ", 2)
{
    Scope (\_SB_)
    {
        // Peripheral device nodes TBD for ACPI/Windows ARM64 mode
    }
}

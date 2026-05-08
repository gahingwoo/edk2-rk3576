/** @file
 *
 *  RK3576 GPIO Library.
 *
 *  Implements GpioLib using RK3576-specific IOC and GPIO register addresses.
 *
 *  Hardware reference:
 *    - GPIO controllers (direction/value/input):
 *        GPIO0 @ 0x27320000
 *        GPIO1 @ 0x2AE10000, GPIO2 @ 0x2AE20000
 *        GPIO3 @ 0x2AE30000, GPIO4 @ 0x2AE40000
 *      All use the standard Rockchip GPIO IP register layout.
 *
 *    - IOMUX function-select registers are in the single IOC_GRF @ 0x26040000
 *      (0xC000-byte block, covers all five GPIO banks).
 *      Offsets derived from upstream Linux kernel pinctrl-rockchip.c:
 *
 *        GPIO0  PA0-PA7: IOC + 0x0000  (4-pin groups: 0x0000, 0x0004)
 *               PB0-PB7: IOC + 0x0008           (0x0008, 0x000C)
 *               PC0-PC7: IOC + 0x2004           (0x2004, 0x2008)
 *               PD0-PD7: IOC + 0x200C           (0x200C, 0x2010)
 *        GPIO1  all:     IOC + 0x4020 + (pin/4)*4
 *        GPIO2  all:     IOC + 0x4040 + (pin/4)*4
 *        GPIO3  all:     IOC + 0x4060 + (pin/4)*4
 *        GPIO4  PA-PB:   IOC + 0x4080 + (pin/4)*4
 *               PC:      IOC + 0xA390 + ((pin-16)/4)*4
 *               PD:      IOC + 0xB398 + ((pin-24)/4)*4
 *
 *  Copyright (c) 2021, Jared McNeill <jmcneill@invisible.ca>    (original RK3588)
 *  Copyright (c) 2025, ROCK 4D RK3576 Port                     (RK3576 adaptation)
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/GpioLib.h>

/* -----------------------------------------------------------------------
 * GPIO controller (direction / value) registers
 * ----------------------------------------------------------------------- */
#define GPIO_BASE(n)  ((n) == 0 ? 0x27320000UL : (0x2AE10000UL + ((UINTN)(n) - 1) * 0x10000UL))

#define GPIO_SWPORT_DR(Pin)   ((Pin) < 16 ? 0x0000U : 0x0004U)
#define GPIO_SWPORT_DDR(Pin)  ((Pin) < 16 ? 0x0008U : 0x000CU)
#define GPIO_EXT_PORT         0x0070U

/* HIWORD write-mask helpers for the GPIO controller */
#define GPIO_WRITE_MASK(Pin)          (1U << (((Pin) % 16) + 16))
#define GPIO_VALUE_MASK(Pin, Value)   ((UINT32)(Value) << ((Pin) % 16))

/* -----------------------------------------------------------------------
 * IOC_GRF (IOMUX / pull / drive) base
 * ----------------------------------------------------------------------- */
#define RK3576_IOC_GRF_BASE  0x26040000UL

/* IOMUX: 4 bits per pin, 4 pins per 32-bit HIWORD register */
#define IOMUX_SHIFT(Pin)  (((Pin) % 4U) * 4U)
#define IOMUX_MASK(Pin)   (0xFU << (IOMUX_SHIFT (Pin) + 16U))

/* -----------------------------------------------------------------------
 * Compute the IOC_GRF-relative IOMUX register address for (Group, Pin).
 * ----------------------------------------------------------------------- */
STATIC EFI_PHYSICAL_ADDRESS
GpioIomuxReg (
  IN UINT8  Group,
  IN UINT8  Pin
  )
{
  switch (Group) {
    case 0:
      if (Pin < 16) {
        /* PA0-PB7: consecutive starting at IOC offset 0x0000 */
        return RK3576_IOC_GRF_BASE + (UINTN)(Pin / 4) * 4UL;
      } else {
        /* PC0-PD7: base at IOC offset 0x2004 (4-pin stride = 4 bytes) */
        return RK3576_IOC_GRF_BASE + 0x2004UL + (UINTN)((Pin - 16) / 4) * 4UL;
      }

    case 1:  return RK3576_IOC_GRF_BASE + 0x4020UL + (UINTN)(Pin / 4) * 4UL;
    case 2:  return RK3576_IOC_GRF_BASE + 0x4040UL + (UINTN)(Pin / 4) * 4UL;
    case 3:  return RK3576_IOC_GRF_BASE + 0x4060UL + (UINTN)(Pin / 4) * 4UL;

    case 4:
      if (Pin < 16) {
        return RK3576_IOC_GRF_BASE + 0x4080UL + (UINTN)(Pin / 4) * 4UL;
      } else if (Pin < 24) {
        return RK3576_IOC_GRF_BASE + 0xA390UL + (UINTN)((Pin - 16) / 4) * 4UL;
      } else {
        return RK3576_IOC_GRF_BASE + 0xB398UL + (UINTN)((Pin - 24) / 4) * 4UL;
      }

    default:
      ASSERT (FALSE);
      return 0;
  }
}

/* -----------------------------------------------------------------------
 * Direction / value / read
 * ----------------------------------------------------------------------- */
VOID
GpioPinSetDirection (
  IN UINT8               Group,
  IN UINT8               Pin,
  IN GPIO_PIN_DIRECTION  Direction
  )
{
  DEBUG ((
    DEBUG_INFO,
    "GpioPinSetDirection Group:%d Pin:%d Direction:%d\n",
    Group, Pin, Direction
    ));
  MmioWrite32 (
    GPIO_BASE (Group) + GPIO_SWPORT_DDR (Pin),
    GPIO_WRITE_MASK (Pin) | GPIO_VALUE_MASK (Pin, Direction)
    );
}

VOID
GpioPinWrite (
  IN UINT8    Group,
  IN UINT8    Pin,
  IN BOOLEAN  Value
  )
{
  MmioWrite32 (
    GPIO_BASE (Group) + GPIO_SWPORT_DR (Pin),
    GPIO_WRITE_MASK (Pin) | GPIO_VALUE_MASK (Pin, Value)
    );
}

BOOLEAN
GpioPinRead (
  IN UINT8  Group,
  IN UINT8  Pin
  )
{
  CONST UINT32  Value = MmioRead32 (GPIO_BASE (Group) + GPIO_SWPORT_DR (Pin));

  return (Value & GPIO_VALUE_MASK (Pin, 1)) != 0;
}

BOOLEAN
GpioPinReadActual (
  IN UINT8  Group,
  IN UINT8  Pin
  )
{
  CONST UINT32  Value = MmioRead32 (GPIO_BASE (Group) + GPIO_EXT_PORT);

  return (Value & (1U << Pin)) != 0;
}

/* -----------------------------------------------------------------------
 * IOMUX function selection
 * ----------------------------------------------------------------------- */
VOID
GpioPinSetFunction (
  IN UINT8  Group,
  IN UINT8  Pin,
  IN UINT8  Function
  )
{
  EFI_PHYSICAL_ADDRESS  Reg;
  UINT32                Value;

  DEBUG ((
    DEBUG_INFO,
    "GpioPinSetFunction Group:%d Pin:%d Function:%d\n",
    Group, Pin, Function
    ));

  Reg   = GpioIomuxReg (Group, Pin);
  Value = IOMUX_MASK (Pin) | ((UINT32)Function << IOMUX_SHIFT (Pin));

  DEBUG ((DEBUG_VERBOSE, "  -> MmioWrite32(0x%lX, 0x%08X)\n", Reg, Value));
  MmioWrite32 (Reg, Value);
}

/* -----------------------------------------------------------------------
 * Pull / Drive / Schmitt — stubs (not needed for basic GMAC / USB / MMC
 * bring-up; add full tables when driver coverage requires them).
 * ----------------------------------------------------------------------- */
VOID
GpioPinSetPull (
  IN UINT8          Group,
  IN UINT8          Pin,
  IN GPIO_PIN_PULL  Pull
  )
{
  if (Pull == GPIO_PIN_PULL_NONE) {
    return;  /* No-op: reset-default is pull-none for most pins */
  }

  DEBUG ((
    DEBUG_WARN,
    "GpioPinSetPull Group:%d Pin:%d Pull:%d -- NOT YET IMPLEMENTED for RK3576\n",
    Group, Pin, Pull
    ));
}

VOID
GpioPinSetDrive (
  IN UINT8           Group,
  IN UINT8           Pin,
  IN GPIO_PIN_DRIVE  Drive
  )
{
  if (Drive == GPIO_PIN_DRIVE_DEFAULT) {
    return;
  }

  DEBUG ((
    DEBUG_WARN,
    "GpioPinSetDrive Group:%d Pin:%d Drive:%d -- NOT YET IMPLEMENTED for RK3576\n",
    Group, Pin, Drive
    ));
}

VOID
GpioPinSetInput (
  IN UINT8                  Group,
  IN UINT8                  Pin,
  IN GPIO_PIN_INPUT_ENABLE  InputEnable
  )
{
  if (InputEnable == GPIO_PIN_INPUT_DEFAULT) {
    return;
  }

  DEBUG ((
    DEBUG_WARN,
    "GpioPinSetInput Group:%d Pin:%d IE:%d -- NOT YET IMPLEMENTED for RK3576\n",
    Group, Pin, InputEnable
    ));
}

/* -----------------------------------------------------------------------
 * Bulk IOMUX configuration helper
 * ----------------------------------------------------------------------- */
VOID
GpioSetIomuxConfig (
  IN CONST GPIO_IOMUX_CONFIG  *Configs,
  IN UINT32                   NumConfigs
  )
{
  UINT32  Index;

  for (Index = 0; Index < NumConfigs; Index++) {
    CONST GPIO_IOMUX_CONFIG  *Mux = &Configs[Index];

    DEBUG ((DEBUG_INFO, "GPIO: IOMUX for pin '%a'\n", Mux->Name));
    GpioPinSetFunction (Mux->Group, Mux->Pin, Mux->Function);
    GpioPinSetPull (Mux->Group, Mux->Pin, Mux->Pull);
    if (Mux->Drive != GPIO_PIN_DRIVE_DEFAULT) {
      GpioPinSetDrive (Mux->Group, Mux->Pin, Mux->Drive);
    }
  }
}

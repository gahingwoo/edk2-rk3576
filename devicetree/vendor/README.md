# Vendor BSP device trees

Precompiled DTBs from a vendor kernel, offered as an alternative to the
mainline device tree built from `devicetree/mainline/`.

| File | Board | Used by |
|---|---|---|
| `rk3576-rock-4d.dtb` | Radxa ROCK 4D | `Platform/Radxa/ROCK4D/DeviceTree/Vendor.inf` |

The ROCK 4D DTB (`compatible = "radxa,rock-4d", "rockchip,rk3576"`) came into
this tree with the 2026-08-04 restructure. Which vendor kernel and commit it
was built from was not recorded.

CM5-IO has a `Vendor.inf` too, but it is commented out of the board DSC and
FDF, and its DTB is not in this directory. Instructions for adding one are in
the INF's header.

## License

SPDX-License-Identifier: GPL-2.0-only

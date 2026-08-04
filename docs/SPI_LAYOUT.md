# SPI NOR layout (16 MB)

| Offset      | Size       | Content                                         |
|-------------|-----------:|-------------------------------------------------|
| `0x000000`  | 32 KB      | GPT partition table                             |
| `0x008000`  | ~1 MB      | idblock (DDR init blob + U-Boot SPL)            |
| `0x100000`  | ~3 MB      | FIT image (see below)                           |
| `0x400000`+ | rest       | reserved / UEFI variable storage                |

## FIT image contents

The FIT image at offset `0x100000` carries three payloads:

| Payload          | Load address | Purpose                          |
|------------------|--------------|----------------------------------|
| TF-A BL31        | `0x00040000` | EL3 secure monitor               |
| EDK2 UEFI        | `0x00200000` | BL33 — UEFI implementation       |
| Board DTB        | (fdt)        | `rk3576-rock-4d.dtb`             |

The descriptor is generated from
[`scripts/lib/gen_fit_its.py`](../scripts/lib/gen_fit_its.py).

## Boot flow

```
BootROM
  └─> idblock (DDR init + SPL) at 0x008000
        └─> FIT loader at 0x100000
              ├─> BL31 → 0x00040000 (EL3)
              └─> EDK2 → 0x00200000 (EL2, BL33)
                    └─> UEFI Shell / boot manager
```

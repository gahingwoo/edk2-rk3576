# Prebuilt binaries

Build inputs, not build outputs. `scripts/package.sh` reads these; nothing
here is produced by `scripts/build.sh`.

| File | What it is |
|---|---|
| `bl31.elf` | TF-A BL31 v2.14.0, open source. Preferred. |
| `rk3576_bl31_vendor.elf` | Rockchip's BL31, used if `bl31.elf` is absent. |
| `rk3576_ddr.bin` | LPDDR5 init blob v1.09. Needed by `rkdeveloptool db` when flashing over USB. |
| `idblock_mainline.bin` | U-Boot SPL (idbloader). `package.sh` prefers a freshly built `out/<BOARD>/idbloader.img` over this one. |
| `rk3576_spi_nor_gpt.img` | GPT written at offset 0 of the 16 MB SPI NOR image (ROCK 4D). |

`checksums.sha256` covers the ELF and BIN files.

## On the GPT image

It was generated from a Rockchip parameter file that still says
`MACHINE_MODEL: rk3588_s`. Only the partition table it produces is used, and
the layout it describes matches what the RK3576 SPL expects, so it works —
but it has not been regenerated for RK3576 and the provenance is worth
knowing before anyone edits it.

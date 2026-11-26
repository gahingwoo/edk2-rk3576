# features.json and FEATURES.json

What a firmware image supports, read from its build. `scripts/features.py`
writes it; nothing in it is typed by hand.

- `scripts/features.py board <board>`, after `scripts/package.sh`, writes
  `out/<PLATFORM>/features.json`. CI runs it in every firmware job and
  uploads it with the images.
- `scripts/features.py release ...` merges them into the release asset
  `FEATURES.json` ([releasing](releasing.md)).
- `scripts/features.py notes FEATURES.json` prints the notes table.

Schema version: `schema` is 1, `settings.schema` is 1. A change that removes
or renames a field, or changes its meaning, bumps the version. Adding a field
does not.

## Sources

| Source | Read from |
|---|---|
| FV | the FFS files in `Build/<PLATFORM>/RELEASE_GCC/FV/FVMAIN.Fv`, named through `Guid.xref` (which also lists modules that are not in the FV, so it is only used for names) |
| PCD | `#define _PCD_VALUE_*` in the `AutoGen.h` (and `AutoGen.c` arrays) of the modules in the FV; two modules disagreeing fails |
| IFR | the compiled forms (`DEBUG/<vfr>.c`, `<name>Bin[]`) and strings (`OUTPUT/*StrDefs.hpk`) of the modules in the FV |
| SPL | `out/<PLATFORM>/spl.config` (only when `scripts/build-spl.sh` built the SPL), and the bytes of the image |

## features.json

| Field | Meaning | Source |
|---|---|---|
| `schema` | 1 | |
| `board` | `PLATFORM_NAME`, the image name prefix (`ROCK4D`, `CM5IO`, `TaishanPi3M`) | boards/<board>.conf |
| `boardConfig` | the `boards/*.conf` name | |
| `commit` | the commit built | git |
| `firmwareVersion` | the version string the firmware shows (`rk3576-<PLATFORM>-<describe>`) | UTF-16 string in FVMAIN.Fv |
| `bootMedium` | `spi` or `sdcard` | `BOOT_MEDIUM` |
| `images[]` | `file`, `medium`, `size`, `sha256` of each `<PLATFORM>-<medium>.img`; each one must contain `BL33_AP_UEFI.Fv` byte for byte | out/ |
| `spl` | `built`, `armbianOnce`, `armbianOnceSdRequest` | SPL |
| `features` | below | |
| `settings` | below | |

Every feature has `built` (true or false). A runtime option also has
`default` (`on`/`off`, or a mode) when built.

| Feature | `built` when | Extra fields |
|---|---|---|
| `windows` | FV has `AcpiTables` (FILE_GUID 7E374E25) and `RK3576AcpiPlatformDxe`, and `PcdConfigTableModeDefault` has the ACPI bit | `acpiTables`, `configTableDefault` (`acpi`, `fdt`, `acpi+fdt`) |
| `armbianOnce` | FV has `ArmbianOnce`, the SPL has the one-shot (`CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE`, plus `..._SD_REQUEST` where `PcdArmbianOnceRequestOnSd`), and the image contains `ArmbianOnce/1` | `uefiApp`, `splBuilt`, `splTagInImage`, `requestOn` (`emmc`, `sd`), `splSdRequest` |
| `hdmi` | FV has `Vop2Dxe` and `DwHdmiQpLib` | |
| `dsi` | FV has `Vop2Dxe` and `DwMipiDsi2Lib` | |
| `usbcDp` | `PcdUsbcDpSupported` | `default` from `PcdUsbcDpDefault` |
| `cpuIdle` | FV has `RK3576AcpiPlatformDxe` | `default` from `PcdAcpiCpuIdleStatesDefault` |
| `fanControl` | `PcdAcpiFanControlSupported` | `default` from `PcdAcpiFanControlDefault` (`always-on`, `balanced`, `performance`, `quiet`, `off`), `bootDutyPercent` from `PcdPwmFanSpeedDefault` |
| `networkBoot` | FV has `UefiPxeBcDxe` | `default` from `PcdNetworkStack{Enabled,PxeBootEnabled}Default` |
| `httpBoot` | FV has `HttpBootDxe` | `default` from `PcdNetworkStack{Enabled,HttpBootEnabled}Default` |
| `secureBoot` | FV has `SecureBootConfigDxe` | |

`FEATURES_EXPECTED` in `boards/<board>.conf` lists the features the board's
image must have. `features.py board` fails when one of them is not built.

## settings

The setup options stored in an EFI variable, for tools that change them
offline (in the variable store on the boot medium). Source: IFR.

| Field | Meaning |
|---|---|
| `schema` | 1 |
| `options[]` | one per question on a setup page |
| `options[].id` | the variable name, or `<variable>@<offset>` for a field of a larger variable |
| `options[].prompt` | the text on the page, `{"en-US": ..., "zh-Hans": ...}` |
| `options[].menu` | the page path, e.g. `["RK3576 Platform Configuration", "ACPI / Device Tree"]` (a front page entry), or `["Device Manager", ...]` for a page listed there |
| `options[].variable` | `name`, `guid` (vendor GUID), `attributes` (number) and `attributeNames` (`NV`, `BS`, `RT`), `size` (bytes of the whole variable) as the form's varstore declares them |
| `options[].standalone` | true when the option is the whole variable |
| `options[].offset`, `width` | byte offset and size of the value inside the variable; little-endian |
| `options[].type` | `oneof`, `checkbox`, `numeric`, `string`, `orderedlist` |
| `options[].default` | the default the form gives, or for `string` the empty string |
| `options[].defaultSource` | `form`, or the PCD it was read from when the form takes its default from another variable that RK3576Dxe fills from that PCD (the display mode) |
| `options[].values[]` | `oneof`/`orderedlist`/`checkbox`: `value` and `label` (`en-US`, `zh-Hans`) |
| `options[].min`, `max`, `step` | `numeric` |
| `options[].minChars`, `maxChars`, `encoding` | `string` (UCS-2, NUL-terminated; `width` is `maxChars` x 2) |
| `options[].elementWidth`, `containers` | `orderedlist` |
| `options[].resetRequired` | the page asks for a reset after a change |
| `options[].hiddenWhen[]`, `grayedOutWhen[]` | the conditions of the enclosing `suppressif`/`grayoutif`, as text over `<variable>@<offset>` (`expression not decoded` otherwise). Variables named `*Supported` are set at every boot by the firmware, not stored |
| `options[].boardSupport` | for an option hidden by a `<X>Supported` variable: `pcd` (`Pcd<X>Supported`, or null when the firmware probes it at boot) and its `value` in this build |
| `options[].module`, `formset` | the driver and formset GUID |
| `omitted[]` | questions that are not here, with `reason`: kept by a driver through HII config access (no variable of their own), not stored, or no default in the build |

## FEATURES.json

The release asset. `schema`, `tag`, `assetVersion` (the version part of the
image names: the tag, or `ci-<sha7>`), `commit`, and `boards`, keyed by
`board`. Each board has `firmwareVersion`, `bootMedium`, `spl`, `features`,
`settings` and `images[]` with the release file names. It is listed in
`SHA256SUMS.txt`.

[Defines]
  PLATFORM_NAME                  = ROCK4D_TEST
  PLATFORM_VENDOR                = Radxa
  PLATFORM_GUID                  = 9c947b5c-f9ad-4605-8d0d-6d2118fe2660
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/ROCK4D_TEST
  VENDOR_DIRECTORY               = Platform/Radxa
  PLATFORM_DIRECTORY             = $(VENDOR_DIRECTORY)/ROCK4D
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = Silicon/Rockchip/RK3576/RK3576.fdf
  RK_PLATFORM_FVMAIN_MODULES     = $(PLATFORM_DIRECTORY)/ROCK4D.Modules.fdf.inc
  FIRMWARE_VER                   = "0.1"

!include Silicon/Rockchip/RK3588/RK3588Platform.dsc.inc

[LibraryClasses.common]
  RockchipPlatformLib|Platform/Radxa/ROCK4D/Library/RockchipPlatformLib/RockchipPlatformLib.inf

[PcdsFixedAtBuild.common]
  gRockchipTokenSpaceGuid.PcdProcessorName|"Rockchip RK3576"
  gRockchipTokenSpaceGuid.PcdPlatformName|"ROCK 4D"
  gRockchipTokenSpaceGuid.PcdPlatformVendorName|"Radxa"
  gRockchipTokenSpaceGuid.PcdFamilyName|"ROCK 4"
  gRockchipTokenSpaceGuid.PcdProductUrl|"https://radxa.com/products/rock4/4d"
  gRockchipTokenSpaceGuid.PcdDeviceTreeName|"rk3576-rock-4d"

[Components.common]
  $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf
  $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf
  $(VENDOR_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf
  Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/RK3576Dxe.inf

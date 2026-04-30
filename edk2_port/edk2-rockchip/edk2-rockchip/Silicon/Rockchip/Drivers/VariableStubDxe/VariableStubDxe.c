/** @file
 *
 *  In-memory (volatile) Variable architectural protocol provider.
 *
 *  Some Rockchip platforms (e.g. RK3576 ROCK 4D) cannot run RkFvbDxe
 *  yet because the ATAGS region passed by SPL is firewalled by BL31.
 *  Without RkFvbDxe -> VariableRuntimeDxe, the EFI architectural
 *  protocols `Variable` and `VariableWrite` are missing AND the
 *  default gRT->GetVariable / SetVariable function pointers remain
 *  set to `CoreEfiNotAvailableYetArg5` which CpuDeadLoop()s.
 *
 *  Earlier revisions of this driver returned EFI_NOT_FOUND from Get
 *  and silently dropped Set. That breaks console binding: BDS calls
 *  EfiBootManagerUpdateConsoleVariable(ConIn, ...) (Get -> modify ->
 *  Set) and then EfiBootManagerConnectAllDefaultConsoles() reads
 *  ConIn back to know which device path to connect. With drop-Set,
 *  ConIn is always NOT_FOUND -> ConPlatformDxe never aggregates
 *  TerminalDxe into ConSplitter -> ConIn->WaitForKey is silent
 *  forever -> BDS sits in the wait loop, looking "deaf".
 *
 *  This revision keeps a simple in-RAM linked list of variables.
 *  Reads and writes both work; values are lost on reset (no NV).
 *  That is sufficient to keep ConIn / ConOut / ErrOut / BootOrder
 *  consistent within a single boot.
 *
 *  Replace with a real Variable provider once SPI flash and ATAGS
 *  plumbing for RK3576 is in place.
 *
 *  Copyright (c) 2026, ROCK 4D RK3576 Port
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/VariableWrite.h>
#include <Protocol/Variable.h>

typedef struct {
  LIST_ENTRY    Link;
  EFI_GUID      VendorGuid;
  CHAR16        *Name;     // NULL-terminated, owned
  UINT32        Attributes;
  UINTN         DataSize;
  VOID          *Data;     // owned
} STUB_VAR_ENTRY;

STATIC LIST_ENTRY  mVarList = INITIALIZE_LIST_HEAD_VARIABLE (mVarList);
STATIC EFI_TPL     mVarTpl  = TPL_NOTIFY;

STATIC
STUB_VAR_ENTRY *
FindVar (
  IN CONST CHAR16    *Name,
  IN CONST EFI_GUID  *Guid
  )
{
  LIST_ENTRY      *Link;
  STUB_VAR_ENTRY  *Entry;

  for (Link = mVarList.ForwardLink; Link != &mVarList; Link = Link->ForwardLink) {
    Entry = BASE_CR (Link, STUB_VAR_ENTRY, Link);
    if (CompareGuid (&Entry->VendorGuid, Guid) &&
        (StrCmp (Entry->Name, Name) == 0))
    {
      return Entry;
    }
  }

  return NULL;
}

STATIC
VOID
FreeVar (
  IN STUB_VAR_ENTRY  *Entry
  )
{
  RemoveEntryList (&Entry->Link);
  if (Entry->Name != NULL) {
    FreePool (Entry->Name);
  }

  if (Entry->Data != NULL) {
    FreePool (Entry->Data);
  }

  FreePool (Entry);
}

STATIC
EFI_STATUS
EFIAPI
StubGetVariable (
  IN     CHAR16    *VariableName,
  IN     EFI_GUID  *VendorGuid,
  OUT    UINT32    *Attributes  OPTIONAL,
  IN OUT UINTN     *DataSize,
  OUT    VOID      *Data        OPTIONAL
  )
{
  STUB_VAR_ENTRY  *Entry;
  EFI_STATUS      Status;
  EFI_TPL         OldTpl;

  if ((VariableName == NULL) || (VendorGuid == NULL) || (DataSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (VariableName[0] == 0) {
    return EFI_NOT_FOUND;
  }

  OldTpl = gBS->RaiseTPL (mVarTpl);

  Entry = FindVar (VariableName, VendorGuid);
  if (Entry == NULL) {
    Status = EFI_NOT_FOUND;
    goto Done;
  }

  if (Attributes != NULL) {
    *Attributes = Entry->Attributes;
  }

  if (*DataSize < Entry->DataSize) {
    *DataSize = Entry->DataSize;
    Status    = EFI_BUFFER_TOO_SMALL;
    goto Done;
  }

  if (Data == NULL) {
    Status = EFI_INVALID_PARAMETER;
    goto Done;
  }

  CopyMem (Data, Entry->Data, Entry->DataSize);
  *DataSize = Entry->DataSize;
  Status    = EFI_SUCCESS;

Done:
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
StubGetNextVariableName (
  IN OUT UINTN     *VariableNameSize,
  IN OUT CHAR16    *VariableName,
  IN OUT EFI_GUID  *VendorGuid
  )
{
  LIST_ENTRY      *Link;
  STUB_VAR_ENTRY  *Entry;
  STUB_VAR_ENTRY  *Next;
  UINTN           NeededSize;
  EFI_STATUS      Status;
  EFI_TPL         OldTpl;

  if ((VariableNameSize == NULL) || (VariableName == NULL) || (VendorGuid == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  OldTpl = gBS->RaiseTPL (mVarTpl);

  Next = NULL;

  if (VariableName[0] == 0) {
    //
    // Caller asks for the first entry.
    //
    if (!IsListEmpty (&mVarList)) {
      Next = BASE_CR (mVarList.ForwardLink, STUB_VAR_ENTRY, Link);
    }
  } else {
    Entry = FindVar (VariableName, VendorGuid);
    if (Entry == NULL) {
      Status = EFI_INVALID_PARAMETER;
      goto Done;
    }

    Link = Entry->Link.ForwardLink;
    if (Link != &mVarList) {
      Next = BASE_CR (Link, STUB_VAR_ENTRY, Link);
    }
  }

  if (Next == NULL) {
    Status = EFI_NOT_FOUND;
    goto Done;
  }

  NeededSize = StrSize (Next->Name);
  if (*VariableNameSize < NeededSize) {
    *VariableNameSize = NeededSize;
    Status            = EFI_BUFFER_TOO_SMALL;
    goto Done;
  }

  CopyMem (VariableName, Next->Name, NeededSize);
  CopyMem (VendorGuid, &Next->VendorGuid, sizeof (EFI_GUID));
  *VariableNameSize = NeededSize;
  Status            = EFI_SUCCESS;

Done:
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
StubSetVariable (
  IN  CHAR16    *VariableName,
  IN  EFI_GUID  *VendorGuid,
  IN  UINT32    Attributes,
  IN  UINTN     DataSize,
  IN  VOID      *Data
  )
{
  STUB_VAR_ENTRY  *Entry;
  EFI_STATUS      Status;
  EFI_TPL         OldTpl;
  UINTN           NameBytes;

  if ((VariableName == NULL) || (VariableName[0] == 0) || (VendorGuid == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((DataSize != 0) && (Data == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  OldTpl = gBS->RaiseTPL (mVarTpl);

  Entry = FindVar (VariableName, VendorGuid);

  //
  // Delete request: zero size or zero attributes -> remove existing.
  //
  if ((DataSize == 0) ||
      ((Attributes & (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)) == 0))
  {
    if (Entry != NULL) {
      FreeVar (Entry);
      Status = EFI_SUCCESS;
    } else {
      Status = EFI_NOT_FOUND;
    }

    goto Done;
  }

  if (Entry != NULL) {
    //
    // Replace data in place.
    //
    if (Entry->Data != NULL) {
      FreePool (Entry->Data);
      Entry->Data = NULL;
    }

    Entry->Data = AllocateCopyPool (DataSize, Data);
    if (Entry->Data == NULL) {
      FreeVar (Entry);
      Status = EFI_OUT_OF_RESOURCES;
      goto Done;
    }

    Entry->DataSize   = DataSize;
    Entry->Attributes = Attributes;
    Status            = EFI_SUCCESS;
    goto Done;
  }

  //
  // Insert new entry.
  //
  Entry = AllocateZeroPool (sizeof (STUB_VAR_ENTRY));
  if (Entry == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  NameBytes  = StrSize (VariableName);
  Entry->Name = AllocateCopyPool (NameBytes, VariableName);
  Entry->Data = AllocateCopyPool (DataSize, Data);
  if ((Entry->Name == NULL) || (Entry->Data == NULL)) {
    if (Entry->Name != NULL) {
      FreePool (Entry->Name);
    }

    if (Entry->Data != NULL) {
      FreePool (Entry->Data);
    }

    FreePool (Entry);
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }

  CopyMem (&Entry->VendorGuid, VendorGuid, sizeof (EFI_GUID));
  Entry->Attributes = Attributes;
  Entry->DataSize   = DataSize;
  InsertTailList (&mVarList, &Entry->Link);
  Status = EFI_SUCCESS;

Done:
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
StubQueryVariableInfo (
  IN  UINT32  Attributes,
  OUT UINT64  *MaximumVariableStorageSize,
  OUT UINT64  *RemainingVariableStorageSize,
  OUT UINT64  *MaximumVariableSize
  )
{
  if ((MaximumVariableStorageSize == NULL) ||
      (RemainingVariableStorageSize == NULL) ||
      (MaximumVariableSize == NULL))
  {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Advertise plenty of room; we are bounded by AllocatePool.
  //
  *MaximumVariableStorageSize   = SIZE_1MB;
  *RemainingVariableStorageSize = SIZE_1MB;
  *MaximumVariableSize          = SIZE_64KB;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
VariableStubDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  Handle;
  EFI_STATUS  Status;

  //
  // Replace runtime service stubs (CpuDeadLoop) with our in-RAM
  // variable store BEFORE any other driver tries to access NV vars.
  //
  gRT->GetVariable         = StubGetVariable;
  gRT->GetNextVariableName = StubGetNextVariableName;
  gRT->SetVariable         = StubSetVariable;
  gRT->QueryVariableInfo   = StubQueryVariableInfo;

  //
  // Now publish the arch protocols — DxeCore unblocks BDS dispatch.
  //
  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiVariableArchProtocolGuid,      NULL,
                  &gEfiVariableWriteArchProtocolGuid, NULL,
                  NULL
                  );
  DEBUG ((
    DEBUG_WARN,
    "VariableStubDxe: in-RAM Variable services installed (volatile). %r\n",
    Status
    ));
  return Status;
}

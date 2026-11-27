/** @file
 *
 *  Read the name of the default Windows entry from a BCD store
 *  (\EFI\Microsoft\Boot\BCD), so that the firmware's boot option for
 *  Windows can be called "Windows 11" instead of "Windows Boot Manager".
 *
 *  A BCD store is a registry hive ("regf").  The name lives at
 *
 *    Objects\{9dea862c-5cdd-4e70-acc1-f32b344d4795}       the boot manager
 *      \Elements\23000003\Element  REG_SZ   GUID of its default entry
 *    Objects\<that GUID>
 *      \Elements\12000004\Element  REG_SZ   that entry's description
 *
 *  The file is read from a disk and is untrusted: every offset and length
 *  is checked against the buffer, list walks are bounded by their cells,
 *  and nothing is written outside the caller's output buffer.  Big-data
 *  values (over 16344 bytes) are not followed; a description never needs
 *  one.  The transaction logs (BCD.LOG*) are not replayed, so a store whose
 *  last change was never flushed reads as it was before that change.
 *
 *  Plain C with no EDK2 header, so that scripts/tests/bcd-host can build
 *  it on the host.  Hive layout: the regf, hbin, nk, lf/lh/li/ri and vk
 *  records as documented by libyal's "Windows NT Registry File (REGF)
 *  format specification".
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef WINDOWS_BCD_H_
#define WINDOWS_BCD_H_

#ifndef WBCD_INLINE
#ifdef _MSC_VER
#define WBCD_INLINE  static __inline
#else
#define WBCD_INLINE  static inline
#endif
#endif

typedef unsigned char   wbcd_u8;
typedef unsigned short  wbcd_u16;
typedef unsigned int    wbcd_u32;

#define WBCD_BASE_BLOCK_SIZE  4096u
#define WBCD_MAX_BIG_VALUE    16344u
#define WBCD_REG_SZ           1u
#define WBCD_REG_EXPAND_SZ    2u
#define WBCD_KEY_COMP_NAME    0x0020u
#define WBCD_VALUE_COMP_NAME  0x0001u
#define WBCD_BOOTMGR_GUID     "{9dea862c-5cdd-4e70-acc1-f32b344d4795}"

/*
 * Why a lookup failed, for the serial log.
 */
#define WBCD_OK            0
#define WBCD_E_HEADER      1  /* not a regf hive, or a version we do not read */
#define WBCD_E_NO_BOOTMGR  2  /* no Objects\{bootmgr}\Elements\23000003 */
#define WBCD_E_DEFAULT     3  /* the default entry's GUID is not a GUID */
#define WBCD_E_NO_ENTRY    4  /* the default entry or its 12000004 is missing */
#define WBCD_E_NAME        5  /* the description is empty or not printable */

typedef struct {
  const wbcd_u8  *Data;   /* the hive bins, right after the base block */
  wbcd_u32       Size;    /* bytes of Data that are inside the file */
} WBCD_HIVE;

WBCD_INLINE wbcd_u16
WbcdRd16 (const wbcd_u8 *P)
{
  return (wbcd_u16)(P[0] | (P[1] << 8));
}

WBCD_INLINE wbcd_u32
WbcdRd32 (const wbcd_u8 *P)
{
  return (wbcd_u32)P[0] | ((wbcd_u32)P[1] << 8) | ((wbcd_u32)P[2] << 16) | ((wbcd_u32)P[3] << 24);
}

/*
 * The allocated cell at Offset (relative to the first hbin).  Returns its
 * data (after the 4-byte size) and the data's length, or 0.
 */
WBCD_INLINE const wbcd_u8 *
WbcdCell (const WBCD_HIVE *H, wbcd_u32 Offset, wbcd_u32 *Len)
{
  wbcd_u32  Raw;
  wbcd_u32  Size;

  if ((Offset > H->Size) || (H->Size - Offset < 4u) || ((Offset & 7u) != 0)) {
    return 0;
  }

  Raw = WbcdRd32 (H->Data + Offset);
  if ((Raw & 0x80000000u) == 0) {
    return 0;   /* free cell, or not a cell */
  }

  Size = 0u - Raw;
  if ((Size < 8u) || (Size > H->Size - Offset)) {
    return 0;
  }

  *Len = Size - 4u;
  return H->Data + Offset + 4u;
}

WBCD_INLINE int
WbcdLower (int C)
{
  return ((C >= 'A') && (C <= 'Z')) ? C + ('a' - 'A') : C;
}

/*
 * Compare a key or value name stored in the hive (Latin-1 when Compressed,
 * else UTF-16LE) with an ASCII name, ignoring ASCII case.
 */
WBCD_INLINE int
WbcdNameIs (const wbcd_u8 *Name, wbcd_u32 NameLen, int Compressed, const char *Want)
{
  wbcd_u32  Index;
  wbcd_u32  Count;
  wbcd_u32  C;

  Count = Compressed ? NameLen : NameLen / 2u;
  if (!Compressed && ((NameLen & 1u) != 0)) {
    return 0;
  }

  for (Index = 0; Index < Count; Index++) {
    if (Want[Index] == '\0') {
      return 0;
    }

    C = Compressed ? Name[Index] : WbcdRd16 (Name + 2u * Index);
    if ((C > 0x7Fu) || (WbcdLower ((int)C) != WbcdLower ((unsigned char)Want[Index]))) {
      return 0;
    }
  }

  return Want[Count] == '\0';
}

/*
 * An nk record at Offset.  Returns its data and length, or 0.
 */
WBCD_INLINE const wbcd_u8 *
WbcdKey (const WBCD_HIVE *H, wbcd_u32 Offset, wbcd_u32 *Len)
{
  const wbcd_u8  *Nk;

  Nk = WbcdCell (H, Offset, Len);
  if ((Nk == 0) || (*Len < 0x4Cu) || (Nk[0] != 'n') || (Nk[1] != 'k')) {
    return 0;
  }

  if (WbcdRd16 (Nk + 0x48) > *Len - 0x4Cu) {
    return 0;
  }

  return Nk;
}

WBCD_INLINE int
WbcdKeyNameIs (const WBCD_HIVE *H, wbcd_u32 Offset, const char *Want)
{
  const wbcd_u8  *Nk;
  wbcd_u32       Len;

  Nk = WbcdKey (H, Offset, &Len);
  if (Nk == 0) {
    return 0;
  }

  return WbcdNameIs (Nk + 0x4C, WbcdRd16 (Nk + 0x48), (WbcdRd16 (Nk + 2) & WBCD_KEY_COMP_NAME) != 0, Want);
}

/*
 * Search one subkey list (lf, lh, li; ri one level deep) for Want.
 * Returns the subkey's nk offset, or 0 (offset 0 is never a subkey: the
 * first cell of a hive is its root key).
 */
WBCD_INLINE wbcd_u32
WbcdSearchList (const WBCD_HIVE *H, wbcd_u32 ListOffset, const char *Want, int Depth)
{
  const wbcd_u8  *List;
  wbcd_u32       Len;
  wbcd_u32       Count;
  wbcd_u32       Stride;
  wbcd_u32       Index;
  wbcd_u32       Child;
  wbcd_u32       Found;

  List = WbcdCell (H, ListOffset, &Len);
  if ((List == 0) || (Len < 4u)) {
    return 0;
  }

  Count = WbcdRd16 (List + 2);
  if (((List[0] == 'l') && (List[1] == 'f')) || ((List[0] == 'l') && (List[1] == 'h'))) {
    Stride = 8u;
  } else if (((List[0] == 'l') && (List[1] == 'i')) || ((List[0] == 'r') && (List[1] == 'i'))) {
    Stride = 4u;
  } else {
    return 0;
  }

  if (Count > (Len - 4u) / Stride) {
    return 0;
  }

  for (Index = 0; Index < Count; Index++) {
    Child = WbcdRd32 (List + 4u + Index * Stride);
    if (List[0] == 'r') {
      if (Depth > 0) {
        return 0;
      }

      Found = WbcdSearchList (H, Child, Want, Depth + 1);
      if (Found != 0) {
        return Found;
      }
    } else if (WbcdKeyNameIs (H, Child, Want)) {
      return Child;
    }
  }

  return 0;
}

WBCD_INLINE wbcd_u32
WbcdSubkey (const WBCD_HIVE *H, wbcd_u32 KeyOffset, const char *Want)
{
  const wbcd_u8  *Nk;
  wbcd_u32       Len;

  Nk = WbcdKey (H, KeyOffset, &Len);
  if ((Nk == 0) || (WbcdRd32 (Nk + 0x14) == 0)) {
    return 0;
  }

  return WbcdSearchList (H, WbcdRd32 (Nk + 0x1C), Want, 0);
}

/*
 * The REG_SZ value Want of the key at KeyOffset, as UTF-16LE bytes.
 * Returns a pointer into the hive and its byte length, or 0.
 */
WBCD_INLINE const wbcd_u8 *
WbcdStringValue (const WBCD_HIVE *H, wbcd_u32 KeyOffset, const char *Want, wbcd_u32 *Bytes)
{
  const wbcd_u8  *Nk;
  const wbcd_u8  *List;
  const wbcd_u8  *Vk;
  const wbcd_u8  *Data;
  wbcd_u32       Len;
  wbcd_u32       Count;
  wbcd_u32       Index;
  wbcd_u32       Size;
  wbcd_u32       Type;

  Nk = WbcdKey (H, KeyOffset, &Len);
  if (Nk == 0) {
    return 0;
  }

  Count = WbcdRd32 (Nk + 0x24);
  if (Count == 0) {
    return 0;
  }

  List = WbcdCell (H, WbcdRd32 (Nk + 0x28), &Len);
  if ((List == 0) || (Count > Len / 4u)) {
    return 0;
  }

  for (Index = 0; Index < Count; Index++) {
    Vk = WbcdCell (H, WbcdRd32 (List + 4u * Index), &Len);
    if ((Vk == 0) || (Len < 0x14u) || (Vk[0] != 'v') || (Vk[1] != 'k')) {
      continue;
    }

    if (  (WbcdRd16 (Vk + 2) > Len - 0x14u)
       || !WbcdNameIs (Vk + 0x14, WbcdRd16 (Vk + 2), (WbcdRd16 (Vk + 0x10) & WBCD_VALUE_COMP_NAME) != 0, Want))
    {
      continue;
    }

    Type = WbcdRd32 (Vk + 0x0C);
    if ((Type != WBCD_REG_SZ) && (Type != WBCD_REG_EXPAND_SZ)) {
      return 0;
    }

    Size = WbcdRd32 (Vk + 4);
    if ((Size & 0x80000000u) != 0) {
      Size &= 0x7FFFFFFFu;
      if (Size > 4u) {
        return 0;
      }

      Data = Vk + 8;
    } else {
      if (Size > WBCD_MAX_BIG_VALUE) {
        return 0;
      }

      Data = WbcdCell (H, WbcdRd32 (Vk + 8), &Len);
      if ((Data == 0) || (Size > Len)) {
        return 0;
      }
    }

    *Bytes = Size & ~1u;
    return Data;
  }

  return 0;
}

/*
 * Walk Objects\Name\Elements\Element from the root and return the REG_SZ
 * "Element" there.
 */
WBCD_INLINE const wbcd_u8 *
WbcdElement (const WBCD_HIVE *H, wbcd_u32 Root, const char *Object, const char *Element, wbcd_u32 *Bytes)
{
  wbcd_u32  Key;

  Key = WbcdSubkey (H, Root, "Objects");
  Key = (Key != 0) ? WbcdSubkey (H, Key, Object) : 0;
  Key = (Key != 0) ? WbcdSubkey (H, Key, "Elements") : 0;
  Key = (Key != 0) ? WbcdSubkey (H, Key, Element) : 0;
  return (Key != 0) ? WbcdStringValue (H, Key, "Element", Bytes) : 0;
}

WBCD_INLINE int
WbcdIsHex (unsigned C)
{
  return ((C >= '0') && (C <= '9')) || ((C >= 'a') && (C <= 'f')) || ((C >= 'A') && (C <= 'F'));
}

/*
 * The description of the BCD store's default boot entry, e.g. "Windows 11".
 *
 *   File, FileSize   the whole BCD file
 *   Out, OutChars    UTF-16 output, always NUL-terminated when OutChars > 0;
 *                    a longer description is cut to OutChars - 1
 *
 * Returns WBCD_OK or one of WBCD_E_*.  Out is an empty string on failure.
 */
WBCD_INLINE int
WbcdDefaultDescription (const wbcd_u8 *File, wbcd_u32 FileSize, wbcd_u16 *Out, wbcd_u32 OutChars)
{
  WBCD_HIVE       H;
  const wbcd_u8   *Str;
  wbcd_u32        Bytes;
  wbcd_u32        Root;
  wbcd_u32        BinsSize;
  wbcd_u32        Index;
  wbcd_u32        Count;
  wbcd_u32        C;
  char            Guid[39];

  if (OutChars == 0) {
    return WBCD_E_NAME;
  }

  Out[0] = 0;

  if (  (FileSize < WBCD_BASE_BLOCK_SIZE)
     || (File[0] != 'r') || (File[1] != 'e') || (File[2] != 'g') || (File[3] != 'f')
     || (WbcdRd32 (File + 0x14) != 1u))
  {
    return WBCD_E_HEADER;
  }

  BinsSize = WbcdRd32 (File + 0x28);
  H.Data   = File + WBCD_BASE_BLOCK_SIZE;
  H.Size   = FileSize - WBCD_BASE_BLOCK_SIZE;
  if (BinsSize < H.Size) {
    H.Size = BinsSize;
  }

  Root = WbcdRd32 (File + 0x24);

  /*
   * {bootmgr}\23000003: the default entry, "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}".
   */
  Str = WbcdElement (&H, Root, WBCD_BOOTMGR_GUID, "23000003", &Bytes);
  if (Str == 0) {
    return WBCD_E_NO_BOOTMGR;
  }

  for (Index = 0; (Index < Bytes / 2u) && (Index < 38u); Index++) {
    C = WbcdRd16 (Str + 2u * Index);
    if (C == 0) {
      break;
    }

    if ((Index == 0) ? (C != '{')
        : (Index == 37u) ? (C != '}')
        : ((Index == 9u) || (Index == 14u) || (Index == 19u) || (Index == 24u)) ? (C != '-')
        : !WbcdIsHex (C))
    {
      return WBCD_E_DEFAULT;
    }

    Guid[Index] = (char)C;
  }

  if ((Index != 38u) || ((Bytes / 2u > 38u) && (WbcdRd16 (Str + 76) != 0))) {
    return WBCD_E_DEFAULT;
  }

  Guid[38] = '\0';

  /*
   * <default>\12000004: its description.
   */
  Str = WbcdElement (&H, Root, Guid, "12000004", &Bytes);
  if (Str == 0) {
    return WBCD_E_NO_ENTRY;
  }

  Count = 0;
  for (Index = 0; Index < Bytes / 2u; Index++) {
    C = WbcdRd16 (Str + 2u * Index);
    if (C == 0) {
      break;
    }

    if ((C < 0x20u) || (C == 0x7Fu) || ((C >= 0xD800u) && (C <= 0xDFFFu))) {
      Out[0] = 0;
      return WBCD_E_NAME;
    }

    if (Count + 1u < OutChars) {
      Out[Count++] = (wbcd_u16)C;
    }
  }

  while ((Count > 0) && (Out[Count - 1u] == ' ')) {
    Count--;
  }

  Out[Count] = 0;
  return (Count > 0) ? WBCD_OK : WBCD_E_NAME;
}

#endif

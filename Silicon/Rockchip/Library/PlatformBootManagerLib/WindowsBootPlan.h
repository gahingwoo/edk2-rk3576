/** @file
 *
 *  The BootOrder decisions of WindowsBoot.c, as plain C with no EDK2 header
 *  so that scripts/tests/bcd-host can check them on the host:
 *
 *   - WbpMerge: several Boot#### that start bootmgfw.efi from the same
 *     partition become one.  The one kept is the first that carries
 *     bcdboot's optional data (Windows' own entry, which Windows would
 *     otherwise write again), else the first in BootOrder; it takes the
 *     BootOrder slot of the first of them, and the others are deleted.
 *
 *   - WbpSeen / WbpRemember: the partitions the firmware has already put
 *     first once, kept in a firmware variable.  A partition seen before is
 *     never moved to the front again: an entry that comes back (deleted by
 *     hand, or by an OS) is added at the end of BootOrder, and a rename does
 *     not move it.
 *
 *   - WbpDp*: the identity of a boot option for the default boot entry
 *     (DefaultBoot.c).  An option whose device path has a partition node
 *     HD(...) is that partition (GPT/MBR signature, its type and the
 *     partition number) plus the nodes after it, file paths compared
 *     without ASCII case; Windows recreates its entries under new Boot####
 *     numbers and full or short-form paths, and they still match.  Any
 *     other option is its whole device path, byte for byte.  Paths come
 *     from variables and are bounds-checked here.
 *
 *  Copyright (c) 2026, Jiaxing Hu <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef WINDOWS_BOOT_PLAN_H_
#define WINDOWS_BOOT_PLAN_H_

#ifndef WBP_INLINE
#ifdef _MSC_VER
#define WBP_INLINE  static __inline
#else
#define WBP_INLINE  static inline
#endif
#endif

typedef unsigned char   wbp_u8;
typedef unsigned short  wbp_u16;
typedef unsigned int    wbp_u32;

/*
 * Merge the options Match[0..MatchCount) (Boot#### numbers, in BootOrder
 * order; Tagged[i] != 0 when Match[i] carries bcdboot's optional data) in
 * Order[0..*Count).
 *
 * Writes the number kept to *Keep and the numbers to delete to
 * Delete[0..return value), MatchCount - 1 of them at most.  Order and
 * *Count are updated in place: the kept number sits where the first match
 * was, the others are taken out.  Numbers in Order that are not matches,
 * and their order, are left alone.  MatchCount 0 does nothing.
 */
WBP_INLINE wbp_u32
WbpMerge (
  wbp_u16        *Order,
  wbp_u32        *Count,
  const wbp_u16  *Match,
  const wbp_u8   *Tagged,
  wbp_u32        MatchCount,
  wbp_u16        *Keep,
  wbp_u16        *Delete
  )
{
  wbp_u32  Index;
  wbp_u32  M;
  wbp_u32  Next;
  wbp_u32  Deleted;
  int      Placed;
  int      IsMatch;

  if (MatchCount == 0) {
    return 0;
  }

  *Keep = Match[0];
  for (Index = 0; Index < MatchCount; Index++) {
    if (Tagged[Index]) {
      *Keep = Match[Index];
      break;
    }
  }

  Deleted = 0;
  for (Index = 0; Index < MatchCount; Index++) {
    if ((Match[Index] != *Keep)) {
      for (M = 0; M < Deleted; M++) {
        if (Delete[M] == Match[Index]) {
          break;
        }
      }

      if (M == Deleted) {
        Delete[Deleted++] = Match[Index];
      }
    }
  }

  Next   = 0;
  Placed = 0;
  for (Index = 0; Index < *Count; Index++) {
    IsMatch = 0;
    for (M = 0; M < MatchCount; M++) {
      if (Order[Index] == Match[M]) {
        IsMatch = 1;
        break;
      }
    }

    if (!IsMatch) {
      Order[Next++] = Order[Index];
    } else if (!Placed) {
      Order[Next++] = *Keep;
      Placed        = 1;
    }
  }

  *Count = Next;
  return Deleted;
}

/*
 * A partition, as the firmware variable remembers it: the HD() node's
 * signature, its type and the partition number.
 */
typedef struct {
  wbp_u8   Signature[16];
  wbp_u32  PartitionNumber;
  wbp_u8   SignatureType;
  wbp_u8   Reserved[3];
} WBP_PARTITION;

#define WBP_MAX_PARTITIONS  16u

WBP_INLINE int
WbpSamePartition (const WBP_PARTITION *A, const WBP_PARTITION *B)
{
  wbp_u32  Index;

  if ((A->PartitionNumber != B->PartitionNumber) || (A->SignatureType != B->SignatureType)) {
    return 0;
  }

  for (Index = 0; Index < 16u; Index++) {
    if (A->Signature[Index] != B->Signature[Index]) {
      return 0;
    }
  }

  return 1;
}

/* Non-zero when P is in List[0..Count). */
WBP_INLINE int
WbpSeen (const WBP_PARTITION *List, wbp_u32 Count, const WBP_PARTITION *P)
{
  wbp_u32  Index;

  for (Index = 0; Index < Count; Index++) {
    if (WbpSamePartition (&List[Index], P)) {
      return 1;
    }
  }

  return 0;
}

/*
 * Add P to List (room for WBP_MAX_PARTITIONS) unless it is there; when the
 * list is full the oldest goes.  Returns the new count.
 */
WBP_INLINE wbp_u32
WbpRemember (WBP_PARTITION *List, wbp_u32 Count, const WBP_PARTITION *P)
{
  wbp_u32  Index;

  if (Count > WBP_MAX_PARTITIONS) {
    Count = WBP_MAX_PARTITIONS;
  }

  if (WbpSeen (List, Count, P)) {
    return Count;
  }

  if (Count == WBP_MAX_PARTITIONS) {
    for (Index = 1; Index < Count; Index++) {
      List[Index - 1] = List[Index];
    }

    Count--;
  }

  List[Count] = *P;
  return Count + 1;
}

/* ---- device paths ---- */

#define WBP_DP_MEDIA         0x04u
#define WBP_DP_MEDIA_HD      0x01u
#define WBP_DP_MEDIA_FILE    0x04u
#define WBP_DP_END           0x7Fu
#define WBP_DP_END_ENTIRE    0xFFu
#define WBP_DP_HD_SIZE       42u

WBP_INLINE wbp_u32
WbpDpNodeLen (const wbp_u8 *Node)
{
  return (wbp_u32)Node[2] | ((wbp_u32)Node[3] << 8);
}

/*
 * The length of the device path at Dp, end node included, when it is well
 * formed within Size bytes; else 0.
 */
WBP_INLINE wbp_u32
WbpDpSize (const wbp_u8 *Dp, wbp_u32 Size)
{
  wbp_u32  Off;
  wbp_u32  Len;

  Off = 0;
  while (Size - Off >= 4u) {
    Len = WbpDpNodeLen (Dp + Off);
    if ((Len < 4u) || (Len > Size - Off)) {
      return 0;
    }

    if ((Dp[Off] == WBP_DP_END) && (Dp[Off + 1] == WBP_DP_END_ENTIRE)) {
      return (Len == 4u) ? Off + 4u : 0;
    }

    Off += Len;
  }

  return 0;
}

/*
 * Offset of the first HD() node of a well-formed path of Size bytes (from
 * WbpDpSize), or Size when there is none.
 */
WBP_INLINE wbp_u32
WbpDpFindHd (const wbp_u8 *Dp, wbp_u32 Size)
{
  wbp_u32  Off;

  for (Off = 0; Off + 4u < Size; Off += WbpDpNodeLen (Dp + Off)) {
    if (  (Dp[Off] == WBP_DP_MEDIA) && (Dp[Off + 1] == WBP_DP_MEDIA_HD)
       && (WbpDpNodeLen (Dp + Off) >= WBP_DP_HD_SIZE))
    {
      return Off;
    }
  }

  return Size;
}

WBP_INLINE int
WbpDpSameNode (const wbp_u8 *A, const wbp_u8 *B)
{
  wbp_u32  Len;
  wbp_u32  Index;
  wbp_u32  Ca;
  wbp_u32  Cb;

  Len = WbpDpNodeLen (A);
  if ((A[0] != B[0]) || (A[1] != B[1])) {
    return 0;
  }

  if ((A[0] == WBP_DP_MEDIA) && (A[1] == WBP_DP_MEDIA_FILE)) {
    /* UTF-16 text, up to its NUL or the end of the node, ASCII case folded */
    for (Index = 4; ; Index += 2) {
      Ca = (Index + 1u < Len) ? (wbp_u32)(A[Index] | (A[Index + 1] << 8)) : 0;
      Cb = (Index + 1u < WbpDpNodeLen (B)) ? (wbp_u32)(B[Index] | (B[Index + 1] << 8)) : 0;
      if ((Ca >= 'A') && (Ca <= 'Z')) {
        Ca += 'a' - 'A';
      }

      if ((Cb >= 'A') && (Cb <= 'Z')) {
        Cb += 'a' - 'A';
      }

      if (Ca != Cb) {
        return 0;
      }

      if (Ca == 0) {
        return 1;
      }
    }
  }

  if ((A[0] == WBP_DP_MEDIA) && (A[1] == WBP_DP_MEDIA_HD)) {
    /* partition number 4..7, then (after start and size) signature 24..39, MBR type 40, signature type 41 */
    if (WbpDpNodeLen (B) < WBP_DP_HD_SIZE) {
      return 0;
    }

    for (Index = 4; Index < 8u; Index++) {
      if (A[Index] != B[Index]) {
        return 0;
      }
    }

    for (Index = 24; Index < WBP_DP_HD_SIZE; Index++) {
      if (A[Index] != B[Index]) {
        return 0;
      }
    }

    return 1;
  }

  if (Len != WbpDpNodeLen (B)) {
    return 0;
  }

  for (Index = 4; Index < Len; Index++) {
    if (A[Index] != B[Index]) {
      return 0;
    }
  }

  return 1;
}

/*
 * Non-zero when the two device paths (Asize/Bsize bytes of buffer) name the
 * same boot target, as described at the top of this file.  A malformed
 * path matches nothing.
 */
WBP_INLINE int
WbpDpSameTarget (const wbp_u8 *A, wbp_u32 Asize, const wbp_u8 *B, wbp_u32 Bsize)
{
  wbp_u32  Ha;
  wbp_u32  Hb;
  wbp_u32  Index;

  Asize = WbpDpSize (A, Asize);
  Bsize = WbpDpSize (B, Bsize);
  if ((Asize == 0) || (Bsize == 0)) {
    return 0;
  }

  Ha = WbpDpFindHd (A, Asize);
  Hb = WbpDpFindHd (B, Bsize);
  if ((Ha == Asize) != (Hb == Bsize)) {
    return 0;
  }

  if (Ha == Asize) {
    if (Asize != Bsize) {
      return 0;
    }

    for (Index = 0; Index < Asize; Index++) {
      if (A[Index] != B[Index]) {
        return 0;
      }
    }

    return 1;
  }

  /* lockstep from the HD() node to the end node */
  for ( ; ; ) {
    if ((A[Ha] == WBP_DP_END) && (A[Ha + 1] == WBP_DP_END_ENTIRE)) {
      return (B[Hb] == WBP_DP_END) && (B[Hb + 1] == WBP_DP_END_ENTIRE);
    }

    if (!WbpDpSameNode (A + Ha, B + Hb)) {
      return 0;
    }

    Ha += WbpDpNodeLen (A + Ha);
    Hb += WbpDpNodeLen (B + Hb);
  }
}

/* ---- which entry is the default ---- */

/*
 * The setting DefaultBootOs.
 */
#define WBP_DEFAULT_REMEMBERED  0u    /* DefaultBootTarget, else the first Windows found */
#define WBP_DEFAULT_EMMC        1u
#define WBP_DEFAULT_NVME        2u
#define WBP_DEFAULT_SD          3u
#define WBP_DEFAULT_USB         4u
#define WBP_DEFAULT_OFF         255u  /* leave BootOrder as it is */

/*
 * Pick the default among the Windows ESPs present this boot.
 *
 *   Mode           DefaultBootOs
 *   HasTarget      a DefaultBootTarget variable exists (mode 0 then uses
 *                  it, and this returns WBP_PICK_TARGET)
 *   EspKind[i]     WBP_DEFAULT_EMMC.. of each ESP present, in the order
 *                  they were found
 *   EspSeen[i]     the ESP's place in the first-sighting list, or
 *                  WBP_NOT_SEEN
 *
 * Returns the ESP index, WBP_PICK_TARGET, or WBP_PICK_NONE (off, or no
 * ESP of the kind asked for).
 */
#define WBP_NOT_SEEN     0xFFFFFFFFu
#define WBP_PICK_TARGET  0xFFFFFFFEu
#define WBP_PICK_NONE    0xFFFFFFFFu

WBP_INLINE wbp_u32
WbpPickDefault (
  wbp_u32        Mode,
  int            HasTarget,
  const wbp_u8   *EspKind,
  const wbp_u32  *EspSeen,
  wbp_u32        EspCount
  )
{
  wbp_u32  Index;
  wbp_u32  Best;

  if (Mode == WBP_DEFAULT_REMEMBERED) {
    if (HasTarget) {
      return WBP_PICK_TARGET;
    }

    Best = WBP_PICK_NONE;
    for (Index = 0; Index < EspCount; Index++) {
      if ((EspSeen[Index] != WBP_NOT_SEEN) && ((Best == WBP_PICK_NONE) || (EspSeen[Index] < EspSeen[Best]))) {
        Best = Index;
      }
    }

    if ((Best == WBP_PICK_NONE) && (EspCount > 0)) {
      Best = 0;
    }

    return Best;
  }

  if ((Mode >= WBP_DEFAULT_EMMC) && (Mode <= WBP_DEFAULT_USB)) {
    for (Index = 0; Index < EspCount; Index++) {
      if (EspKind[Index] == Mode) {
        return Index;
      }
    }
  }

  return WBP_PICK_NONE;
}

/*
 * The place of P in List, or WBP_NOT_SEEN.
 */
WBP_INLINE wbp_u32
WbpSeenIndex (const WBP_PARTITION *List, wbp_u32 Count, const WBP_PARTITION *P)
{
  wbp_u32  Index;

  for (Index = 0; Index < Count; Index++) {
    if (WbpSamePartition (&List[Index], P)) {
      return Index;
    }
  }

  return WBP_NOT_SEEN;
}

#endif

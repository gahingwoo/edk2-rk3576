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

#endif

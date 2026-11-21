/** @file
 *
 *  SNTP (RFC 4330) and DNS A-query packets.  No UEFI services and no library
 *  calls, so HostTest/ can compile this file unchanged.
 *
 *  Copyright (c) 2026, gahingwoo <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "SntpPacket.h"

#define NTP_MODE_CLIENT  3
#define NTP_MODE_SERVER  4
#define NTP_VERSION      4

#define DNS_HEADER_SIZE  12
#define DNS_TYPE_A       1
#define DNS_CLASS_IN     1
#define DNS_MAX_NAME     253

STATIC
UINT16
GetBe16 (
  IN CONST UINT8  *P
  )
{
  return (UINT16)(((UINT16)P[0] << 8) | P[1]);
}

STATIC
UINT64
GetBe64 (
  IN CONST UINT8  *P
  )
{
  UINT64  Value;
  UINTN   Index;

  Value = 0;
  for (Index = 0; Index < 8; Index++) {
    Value = (Value << 8) | P[Index];
  }

  return Value;
}

STATIC
VOID
PutBe64 (
  OUT UINT8   *P,
  IN  UINT64  Value
  )
{
  UINTN  Index;

  for (Index = 0; Index < 8; Index++) {
    P[7 - Index] = (UINT8)(Value & 0xFF);
    Value      >>= 8;
  }
}

VOID
SntpBuildRequest (
  OUT UINT8   *Packet,
  IN  UINT64  Nonce
  )
{
  UINTN  Index;

  for (Index = 0; Index < SNTP_PACKET_SIZE; Index++) {
    Packet[Index] = 0;
  }

  Packet[0] = (UINT8)((0 << 6) | (NTP_VERSION << 3) | NTP_MODE_CLIENT);
  PutBe64 (&Packet[40], Nonce);
}

RETURN_STATUS
SntpParseReply (
  IN  CONST UINT8  *Packet,
  IN  UINTN        Length,
  IN  UINT64       Nonce,
  OUT SNTP_REPLY   *Reply
  )
{
  UINT8  Mode;

  if ((Packet == NULL) || (Reply == NULL)) {
    return RETURN_INVALID_PARAMETER;
  }

  if (Length < SNTP_PACKET_SIZE) {
    return RETURN_BAD_BUFFER_SIZE;
  }

  Reply->LeapIndicator = (UINT8)(Packet[0] >> 6);
  Reply->Version       = (UINT8)((Packet[0] >> 3) & 0x7);
  Mode                 = (UINT8)(Packet[0] & 0x7);
  Reply->Stratum       = Packet[1];
  Reply->Receive       = GetBe64 (&Packet[32]);
  Reply->Transmit      = GetBe64 (&Packet[40]);

  if ((Mode != NTP_MODE_SERVER) || (Reply->Version < 1) || (Reply->Version > 4)) {
    return RETURN_UNSUPPORTED;
  }

  //
  // The originate timestamp is the only thing that ties this reply to our
  // request; anything else could be a stray or spoofed datagram.
  //
  if (GetBe64 (&Packet[24]) != Nonce) {
    return RETURN_UNSUPPORTED;
  }

  if ((Reply->LeapIndicator == 3) || (Reply->Stratum == 0) || (Reply->Stratum > 15) ||
      (Reply->Transmit == 0))
  {
    return RETURN_NOT_READY;
  }

  return RETURN_SUCCESS;
}

UINT64
SntpNtpSecondsToUnix (
  IN UINT32  NtpSeconds,
  IN UINT64  PivotUnix
  )
{
  UINT64  Unix;
  UINT64  Pivot1900;

  //
  // Work from 1900 so nothing goes negative: Seconds1900 = NtpSeconds +
  // Era * 2^32, with the smallest era that reaches the pivot.
  //
  Pivot1900 = PivotUnix + SNTP_UNIX_EPOCH_DELTA;
  Unix      = NtpSeconds;
  if (Unix < Pivot1900) {
    Unix += ((Pivot1900 - Unix + 0xFFFFFFFFULL) >> 32) << 32;
  }

  return Unix - SNTP_UNIX_EPOCH_DELTA;
}

STATIC
UINT64
FractionToNs (
  IN UINT32  Fraction
  )
{
  return ((UINT64)Fraction * SNTP_NS_PER_SEC) >> 32;
}

UINT64
SntpTimestampToUnixNs (
  IN UINT64  Timestamp,
  IN UINT64  PivotUnix
  )
{
  return SntpNtpSecondsToUnix ((UINT32)(Timestamp >> 32), PivotUnix) * SNTP_NS_PER_SEC +
         FractionToNs ((UINT32)Timestamp);
}

UINT64
SntpServerHoldNs (
  IN CONST SNTP_REPLY  *Reply
  )
{
  UINT64  Diff;

  if (Reply->Receive == 0) {
    return 0;
  }

  //
  // Unsigned 32.32 difference: wraps correctly across an era boundary.
  //
  Diff = Reply->Transmit - Reply->Receive;
  if ((Diff >> 32) != 0) {
    return 0;   // negative (huge when unsigned) or a second or more
  }

  return FractionToNs ((UINT32)Diff);
}

UINT64
SntpReplyTimeUnixNs (
  IN CONST SNTP_REPLY  *Reply,
  IN UINT64            ElapsedNs,
  IN UINT64            PivotUnix
  )
{
  UINT64  Hold;
  UINT64  Delay;

  Hold  = SntpServerHoldNs (Reply);
  Delay = (ElapsedNs > Hold) ? ElapsedNs - Hold : 0;

  return SntpTimestampToUnixNs (Reply->Transmit, PivotUnix) + Delay / 2;
}

UINTN
DnsBuildQuery (
  OUT UINT8        *Buffer,
  IN  UINTN        BufferSize,
  IN  UINT16       Id,
  IN  CONST CHAR8  *Name
  )
{
  UINTN  Pos;
  UINTN  LabelStart;
  UINTN  NameLen;
  UINTN  Index;
  UINT8  Char;

  if ((Buffer == NULL) || (Name == NULL)) {
    return 0;
  }

  for (NameLen = 0; Name[NameLen] != '\0'; NameLen++) {
    if (NameLen > DNS_MAX_NAME) {
      return 0;
    }
  }

  if ((NameLen > 0) && (Name[NameLen - 1] == '.')) {
    NameLen--;
  }

  //
  // Header, the name as labels (one length byte more than the dotted form,
  // plus the root label), then QTYPE and QCLASS.
  //
  if ((NameLen == 0) || (NameLen > DNS_MAX_NAME) ||
      (BufferSize < DNS_HEADER_SIZE + NameLen + 2 + 4))
  {
    return 0;
  }

  Buffer[0]  = (UINT8)(Id >> 8);
  Buffer[1]  = (UINT8)Id;
  Buffer[2]  = 0x01;                      // RD: recursion desired
  Buffer[3]  = 0x00;
  Buffer[4]  = 0x00;
  Buffer[5]  = 0x01;                      // QDCOUNT 1
  Buffer[6]  = 0x00;
  Buffer[7]  = 0x00;
  Buffer[8]  = 0x00;
  Buffer[9]  = 0x00;
  Buffer[10] = 0x00;
  Buffer[11] = 0x00;

  Pos        = DNS_HEADER_SIZE;
  LabelStart = Pos++;
  for (Index = 0; Index <= NameLen; Index++) {
    Char = (Index < NameLen) ? (UINT8)Name[Index] : '.';
    if (Char == '.') {
      if ((Pos - LabelStart - 1 == 0) || (Pos - LabelStart - 1 > 63)) {
        return 0;                         // empty or overlong label
      }

      Buffer[LabelStart] = (UINT8)(Pos - LabelStart - 1);
      LabelStart         = Pos++;
      continue;
    }

    if (!(((Char >= 'a') && (Char <= 'z')) || ((Char >= 'A') && (Char <= 'Z')) ||
          ((Char >= '0') && (Char <= '9')) || (Char == '-') || (Char == '_')))
    {
      return 0;
    }

    Buffer[Pos++] = Char;
  }

  Buffer[LabelStart] = 0;                 // root label, already counted in Pos
  Buffer[Pos++]      = 0;
  Buffer[Pos++]      = DNS_TYPE_A;
  Buffer[Pos++]      = 0;
  Buffer[Pos++]      = DNS_CLASS_IN;

  return Pos;
}

/**
  Step over a (possibly compressed) name.

  @return  Offset just after the name, or 0 when it runs off the buffer.
**/
STATIC
UINTN
DnsSkipName (
  IN CONST UINT8  *Buffer,
  IN UINTN        Length,
  IN UINTN        Pos
  )
{
  UINTN  Labels;
  UINT8  Len;

  for (Labels = 0; Labels < 128; Labels++) {
    if (Pos >= Length) {
      return 0;
    }

    Len = Buffer[Pos];
    if (Len == 0) {
      return Pos + 1;
    }

    if ((Len & 0xC0) == 0xC0) {
      //
      // A pointer ends the name in this position; where it points does not
      // matter for skipping, only that both bytes are there.
      //
      return (Pos + 2 <= Length) ? Pos + 2 : 0;
    }

    if ((Len & 0xC0) != 0) {
      return 0;                           // reserved label types
    }

    Pos += 1 + Len;
  }

  return 0;
}

UINTN
DnsParseReply (
  IN  CONST UINT8  *Buffer,
  IN  UINTN        Length,
  IN  UINT16       Id,
  OUT UINT8        *Addresses,
  IN  UINTN        MaxCount
  )
{
  UINTN   Pos;
  UINTN   Answers;
  UINTN   Index;
  UINTN   Found;
  UINT16  Type;
  UINT16  Class;
  UINT16  RdLength;

  if ((Buffer == NULL) || (Addresses == NULL) || (Length < DNS_HEADER_SIZE)) {
    return 0;
  }

  if ((GetBe16 (&Buffer[0]) != Id) ||
      ((Buffer[2] & 0x80) == 0) ||        // QR: must be a response
      ((Buffer[2] & 0x78) != 0) ||        // OPCODE: standard query
      ((Buffer[3] & 0x0F) != 0) ||        // RCODE: no error
      (GetBe16 (&Buffer[4]) != 1))        // QDCOUNT: our one question
  {
    return 0;
  }

  Answers = GetBe16 (&Buffer[6]);

  Pos = DnsSkipName (Buffer, Length, DNS_HEADER_SIZE);
  if ((Pos == 0) || (Pos + 4 > Length)) {
    return 0;
  }

  if ((GetBe16 (&Buffer[Pos]) != DNS_TYPE_A) || (GetBe16 (&Buffer[Pos + 2]) != DNS_CLASS_IN)) {
    return 0;
  }

  Pos  += 4;
  Found = 0;
  for (Index = 0; (Index < Answers) && (Found < MaxCount); Index++) {
    Pos = DnsSkipName (Buffer, Length, Pos);
    if ((Pos == 0) || (Pos + 10 > Length)) {
      break;
    }

    Type     = GetBe16 (&Buffer[Pos]);
    Class    = GetBe16 (&Buffer[Pos + 2]);
    RdLength = GetBe16 (&Buffer[Pos + 8]);
    Pos     += 10;
    if (Pos + RdLength > Length) {
      break;
    }

    if ((Type == DNS_TYPE_A) && (Class == DNS_CLASS_IN) && (RdLength == 4)) {
      Addresses[Found * 4 + 0] = Buffer[Pos + 0];
      Addresses[Found * 4 + 1] = Buffer[Pos + 1];
      Addresses[Found * 4 + 2] = Buffer[Pos + 2];
      Addresses[Found * 4 + 3] = Buffer[Pos + 3];
      Found++;
    }

    Pos += RdLength;
  }

  return Found;
}

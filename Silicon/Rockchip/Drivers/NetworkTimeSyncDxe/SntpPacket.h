/** @file
 *
 *  SNTP (RFC 4330) and DNS A-query packets, built and checked without any
 *  UEFI service, so that the same file compiles in the firmware and in the
 *  host-side test (HostTest/).
 *
 *  Copyright (c) 2026, gahingwoo <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef SNTP_PACKET_H_
#define SNTP_PACKET_H_

#include <Base.h>

#define SNTP_PORT         123
#define DNS_PORT          53
#define SNTP_PACKET_SIZE  48

//
// Seconds from 1900-01-01 (NTP era 0) to 1970-01-01.
//
#define SNTP_UNIX_EPOCH_DELTA  2208988800ULL

#define SNTP_NS_PER_SEC  1000000000ULL

typedef struct {
  UINT8     LeapIndicator;
  UINT8     Version;
  UINT8     Stratum;
  UINT64    Receive;    // server receive timestamp, NTP 32.32, 0 when absent
  UINT64    Transmit;   // server transmit timestamp, NTP 32.32, never 0
} SNTP_REPLY;

/**
  Fill in a client request: LI 0, VN 4, mode 3, and the transmit timestamp
  set to Nonce, which the server copies to the originate timestamp of its
  reply.  RFC 4330 lets a client put anything there; a value the server
  cannot guess is what makes the reply matchable.

  @param[out] Packet  SNTP_PACKET_SIZE bytes.
  @param[in]  Nonce   Must not be 0.
**/
VOID
SntpBuildRequest (
  OUT UINT8   *Packet,
  IN  UINT64  Nonce
  );

/**
  Check a server reply the way RFC 4330 section 5 asks a client to.

  Rejected: shorter than 48 bytes, mode other than 4 (server), version
  outside 1..4, leap indicator 3 (server clock not synchronised), stratum 0
  (kiss-o'-death) or above 15, an originate timestamp that is not Nonce,
  and a zero transmit timestamp.

  @retval RETURN_SUCCESS            Reply is usable and filled in.
  @retval RETURN_INVALID_PARAMETER  NULL argument.
  @retval RETURN_BAD_BUFFER_SIZE    Too short.
  @retval RETURN_UNSUPPORTED        Not a server reply, or not ours.
  @retval RETURN_NOT_READY          Server says it is not synchronised.
**/
RETURN_STATUS
SntpParseReply (
  IN  CONST UINT8  *Packet,
  IN  UINTN        Length,
  IN  UINT64       Nonce,
  OUT SNTP_REPLY   *Reply
  );

/**
  Convert the 32-bit seconds of an NTP timestamp to Unix seconds.

  The NTP seconds field wraps every 2^32 s (era 0 ends 2036-02-07).  The era
  is chosen so that the result is the first one at or after PivotUnix: with
  the firmware build date as the pivot, any honest server answer lands in
  [build date, build date + 136 years).

  @param[in] NtpSeconds  The seconds half of an NTP timestamp.
  @param[in] PivotUnix   Earliest plausible time, Unix seconds.

  @return  Unix seconds.
**/
UINT64
SntpNtpSecondsToUnix (
  IN UINT32  NtpSeconds,
  IN UINT64  PivotUnix
  );

/**
  An NTP timestamp (32.32) as Unix nanoseconds, era chosen as in
  SntpNtpSecondsToUnix().
**/
UINT64
SntpTimestampToUnixNs (
  IN UINT64  Timestamp,
  IN UINT64  PivotUnix
  );

/**
  How long the server held the request (transmit - receive), in ns.

  0 when the server left the receive timestamp out, or when the difference
  is negative or a second or more: neither is a believable hold time, and
  counting it would move the clock by more than it can be wrong.
**/
UINT64
SntpServerHoldNs (
  IN CONST SNTP_REPLY  *Reply
  );

/**
  The server's time at the moment the reply arrived, in Unix ns.

  RFC 4330 section 5: the round trip is (T4 - T1) - (T3 - T2) and the reply
  spent half of it on the way back, so the time at T4 is T3 + delay / 2.
  The local clock only has to measure the interval T4 - T1.

  @param[in] Reply        A reply SntpParseReply() accepted.
  @param[in] ElapsedNs    T4 - T1 measured locally.
  @param[in] PivotUnix    See SntpNtpSecondsToUnix().
**/
UINT64
SntpReplyTimeUnixNs (
  IN CONST SNTP_REPLY  *Reply,
  IN UINT64            ElapsedNs,
  IN UINT64            PivotUnix
  );

/**
  Build a recursive DNS query for the A record of Name.

  @param[out] Buffer      Where the query goes.
  @param[in]  BufferSize  Its size.
  @param[in]  Id          Query ID, echoed by the server.
  @param[in]  Name        "pool.ntp.org"; labels of 1..63 bytes, 253 bytes
                          at most, no trailing dot needed.

  @return  Query length in bytes, 0 when Name is not a valid host name or
           the buffer is too small.
**/
UINTN
DnsBuildQuery (
  OUT UINT8        *Buffer,
  IN  UINTN        BufferSize,
  IN  UINT16       Id,
  IN  CONST CHAR8  *Name
  );

/**
  Take the IPv4 addresses out of the answer to DnsBuildQuery().

  The reply must carry Id, be a response with RCODE 0 and one question.
  CNAME and other records in the answer section are skipped; every A/IN
  record with 4 bytes of data is collected.  Every read is bounds-checked
  and compression pointers may only point backwards.

  @param[in]  Buffer     Reply.
  @param[in]  Length     Its length.
  @param[in]  Id         ID of the query.
  @param[out] Addresses  MaxCount entries of 4 bytes, network order.
  @param[in]  MaxCount   Entries in Addresses.

  @return  Number of addresses stored (0 on any error).
**/
UINTN
DnsParseReply (
  IN  CONST UINT8  *Buffer,
  IN  UINTN        Length,
  IN  UINT16       Id,
  OUT UINT8        *Addresses,
  IN  UINTN        MaxCount
  );

#endif // SNTP_PACKET_H_

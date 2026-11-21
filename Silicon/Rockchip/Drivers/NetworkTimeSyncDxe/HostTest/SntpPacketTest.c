/** @file
 *
 *  Host-side test of SntpPacket.c and of the TimeBaseLib conversions
 *  NetworkTimeSyncDxe relies on.  Both are compiled unchanged against the
 *  real MdePkg headers; see run.sh.
 *
 *    SntpPacketTest                 offline checks, exit status 0 on success
 *    SntpPacketTest ntp A.B.C.D     one real SNTP exchange, prints the result
 *    SntpPacketTest dns A.B.C.D N   resolve N through the DNS server A.B.C.D
 *    SntpPacketTest epoch           read Unix seconds from stdin, print the
 *                                   EFI_TIME EpochToEfiTime() makes of each
 *
 *  Copyright (c) 2026, gahingwoo <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi/UefiBaseType.h>
#include <Uefi/UefiSpec.h>
#include <Library/TimeBaseLib.h>

#include "../SntpPacket.h"

#undef NULL
#undef TRUE
#undef FALSE
#undef MIN
#undef MAX

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int  mFailures;

#define CHECK(Cond)                                                     \
  do {                                                                  \
    if (!(Cond)) {                                                      \
      printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #Cond);           \
      mFailures++;                                                      \
    }                                                                   \
  } while (0)

static void
PutBe64 (UINT8 *P, UINT64 V)
{
  for (int i = 7; i >= 0; i--) {
    P[i] = (UINT8)V;
    V  >>= 8;
  }
}

// 2026-01-01 00:00:00 UTC, the pivot a 2026 build uses.
#define PIVOT_2026  1767225600ULL

static void
MakeReply (UINT8 *P, UINT8 Li, UINT8 Vn, UINT8 Mode, UINT8 Stratum, UINT64 Orig, UINT64 Rx, UINT64 Tx)
{
  memset (P, 0, SNTP_PACKET_SIZE);
  P[0] = (UINT8)((Li << 6) | (Vn << 3) | Mode);
  P[1] = Stratum;
  PutBe64 (&P[24], Orig);
  PutBe64 (&P[32], Rx);
  PutBe64 (&P[40], Tx);
}

static void
TestRequest (void)
{
  UINT8  P[SNTP_PACKET_SIZE];

  memset (P, 0xAA, sizeof (P));
  SntpBuildRequest (P, 0x0102030405060708ULL);
  CHECK (P[0] == 0x23);               // LI 0, VN 4, mode 3
  for (int i = 1; i < 40; i++) {
    CHECK (P[i] == 0);
  }

  CHECK (P[40] == 1 && P[47] == 8);
}

static void
TestReply (void)
{
  UINT8       P[SNTP_PACKET_SIZE + 4];
  SNTP_REPLY  R;
  UINT64      Nonce = 0x1122334455667788ULL;
  UINT64      Tx    = ((UINT64)(PIVOT_2026 + SNTP_UNIX_EPOCH_DELTA + 100) << 32) | 0x80000000;
  UINT64      Rx    = Tx - 0x40000000;   // held 0.25 s

  MakeReply (P, 0, 4, 4, 2, Nonce, Rx, Tx);
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE, Nonce, &R) == RETURN_SUCCESS);
  CHECK (R.Stratum == 2 && R.Transmit == Tx && R.Receive == Rx);
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE + 4, Nonce, &R) == RETURN_SUCCESS);  // extension fields
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE - 1, Nonce, &R) == RETURN_BAD_BUFFER_SIZE);
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE, Nonce + 1, &R) == RETURN_UNSUPPORTED);
  CHECK (SntpParseReply (NULL, SNTP_PACKET_SIZE, Nonce, &R) == RETURN_INVALID_PARAMETER);

  MakeReply (P, 0, 3, 4, 1, Nonce, Rx, Tx);  // NTPv3 server
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE, Nonce, &R) == RETURN_SUCCESS);
  MakeReply (P, 1, 4, 4, 15, Nonce, Rx, Tx); // leap second pending, stratum 15
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE, Nonce, &R) == RETURN_SUCCESS);

  MakeReply (P, 0, 4, 3, 2, Nonce, Rx, Tx);  // a client packet (our own echoed)
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE, Nonce, &R) == RETURN_UNSUPPORTED);
  MakeReply (P, 0, 4, 5, 2, Nonce, Rx, Tx);  // broadcast
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE, Nonce, &R) == RETURN_UNSUPPORTED);
  MakeReply (P, 0, 0, 4, 2, Nonce, Rx, Tx);  // version 0
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE, Nonce, &R) == RETURN_UNSUPPORTED);
  MakeReply (P, 0, 5, 4, 2, Nonce, Rx, Tx);  // version 5
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE, Nonce, &R) == RETURN_UNSUPPORTED);
  MakeReply (P, 3, 4, 4, 2, Nonce, Rx, Tx);  // alarm: not synchronised
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE, Nonce, &R) == RETURN_NOT_READY);
  MakeReply (P, 0, 4, 4, 0, Nonce, Rx, Tx);  // kiss-o'-death
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE, Nonce, &R) == RETURN_NOT_READY);
  MakeReply (P, 0, 4, 4, 16, Nonce, Rx, Tx); // stratum 16: unsynchronised
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE, Nonce, &R) == RETURN_NOT_READY);
  MakeReply (P, 0, 4, 4, 2, Nonce, Rx, 0);   // no transmit timestamp
  CHECK (SntpParseReply (P, SNTP_PACKET_SIZE, Nonce, &R) == RETURN_NOT_READY);
}

static void
TestEra (void)
{
  // Era 0, 2026-11-22 12:00:00 UTC = 1795348800.
  CHECK (SntpNtpSecondsToUnix ((UINT32)(1795348800ULL + SNTP_UNIX_EPOCH_DELTA), PIVOT_2026) == 1795348800ULL);
  // The pivot itself.
  CHECK (SntpNtpSecondsToUnix ((UINT32)(PIVOT_2026 + SNTP_UNIX_EPOCH_DELTA), PIVOT_2026) == PIVOT_2026);
  // Last second of era 0: 2036-02-07 06:28:15.
  CHECK (SntpNtpSecondsToUnix (0xFFFFFFFFu, PIVOT_2026) == 2085978495ULL);
  // Era 1 begins: NTP seconds wrap to 0 at 2036-02-07 06:28:16.
  CHECK (SntpNtpSecondsToUnix (0, PIVOT_2026) == 2085978496ULL);
  CHECK (SntpNtpSecondsToUnix (100, PIVOT_2026) == 2085978596ULL);
  // A 1990 timestamp is before a 2026 pivot, so it reads as era 1 (2126);
  // NetworkTimeSyncDxe's upper bound on the year is what rejects it.
  CHECK (SntpNtpSecondsToUnix ((UINT32)(631152000ULL + SNTP_UNIX_EPOCH_DELTA), PIVOT_2026) ==
         631152000ULL + 0x100000000ULL);
  // A build after 2036 reads era-1 values without help.
  CHECK (SntpNtpSecondsToUnix (1000, 2085978496ULL + 500) == 2085978496ULL + 1000);
  CHECK (SntpNtpSecondsToUnix (100, 2085978496ULL + 500) == 2085978496ULL + 100 + 0x100000000ULL);

  CHECK (SntpTimestampToUnixNs (((UINT64)(PIVOT_2026 + SNTP_UNIX_EPOCH_DELTA) << 32) | 0x80000000u, PIVOT_2026) ==
         PIVOT_2026 * 1000000000ULL + 500000000ULL);
  CHECK (SntpTimestampToUnixNs (((UINT64)(PIVOT_2026 + SNTP_UNIX_EPOCH_DELTA) << 32) | 0xFFFFFFFFu, PIVOT_2026) ==
         PIVOT_2026 * 1000000000ULL + 999999999ULL);
}

static void
TestDelay (void)
{
  SNTP_REPLY  R;
  UINT64      Base = ((UINT64)(PIVOT_2026 + SNTP_UNIX_EPOCH_DELTA) << 32);

  memset (&R, 0, sizeof (R));
  R.Transmit = Base + 0x40000000;          // .25
  R.Receive  = Base;
  CHECK (SntpServerHoldNs (&R) == 250000000ULL);
  R.Receive = 0;
  CHECK (SntpServerHoldNs (&R) == 0);      // not given
  R.Receive = R.Transmit + 1;
  CHECK (SntpServerHoldNs (&R) == 0);      // negative
  R.Receive = R.Transmit - (1ULL << 32);
  CHECK (SntpServerHoldNs (&R) == 0);      // a whole second
  // Receive just before an era wrap, transmit just after: 0.5 s.
  R.Receive  = 0xFFFFFFFFC0000000ULL;
  R.Transmit = 0x0000000040000000ULL;
  CHECK (SntpServerHoldNs (&R) == 500000000ULL);

  // 100 ms round trip, 20 ms of it in the server: 40 ms on the way back.
  R.Transmit = Base;
  R.Receive  = Base - ((20ULL << 32) / 1000);
  CHECK (SntpReplyTimeUnixNs (&R, 100000000ULL, PIVOT_2026) / 1000000ULL ==
         PIVOT_2026 * 1000ULL + 40);
  // A local interval shorter than the hold time must not go negative.
  CHECK (SntpReplyTimeUnixNs (&R, 1000ULL, PIVOT_2026) == PIVOT_2026 * 1000000000ULL);
}

static void
TestDnsQuery (void)
{
  UINT8        B[512];
  static const UINT8  Expect[] = {
    0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    4, 'p', 'o', 'o', 'l', 3, 'n', 't', 'p', 3, 'o', 'r', 'g', 0,
    0x00, 0x01, 0x00, 0x01
  };
  char         Long[80];

  CHECK (DnsBuildQuery (B, sizeof (B), 0x1234, "pool.ntp.org") == sizeof (Expect));
  CHECK (memcmp (B, Expect, sizeof (Expect)) == 0);
  CHECK (DnsBuildQuery (B, sizeof (B), 0x1234, "pool.ntp.org.") == sizeof (Expect));
  CHECK (memcmp (B, Expect, sizeof (Expect)) == 0);
  CHECK (DnsBuildQuery (B, sizeof (Expect), 0x1234, "pool.ntp.org") == sizeof (Expect));
  CHECK (DnsBuildQuery (B, sizeof (Expect) - 1, 0x1234, "pool.ntp.org") == 0);
  CHECK (DnsBuildQuery (B, sizeof (B), 1, "") == 0);
  CHECK (DnsBuildQuery (B, sizeof (B), 1, ".") == 0);
  CHECK (DnsBuildQuery (B, sizeof (B), 1, "a..b") == 0);
  CHECK (DnsBuildQuery (B, sizeof (B), 1, ".a") == 0);
  CHECK (DnsBuildQuery (B, sizeof (B), 1, "a b") == 0);
  memset (Long, 'x', 63);
  Long[63] = 0;
  CHECK (DnsBuildQuery (B, sizeof (B), 1, Long) == 12 + 1 + 63 + 1 + 4);
  memset (Long, 'x', 64);
  Long[64] = 0;
  CHECK (DnsBuildQuery (B, sizeof (B), 1, Long) == 0);
  CHECK (DnsBuildQuery (B, sizeof (B), 1, "time.cloudflare.com") == 12 + 21 + 4);
}

static size_t
MakeDnsReply (UINT8 *B, UINT16 Id, UINT8 Rcode, int Answers)
{
  size_t  N = DnsBuildQuery (B, 512, Id, "pool.ntp.org");

  B[2] = 0x81;                // QR, RD
  B[3] = (UINT8)(0x80 | Rcode); // RA
  B[7] = (UINT8)Answers;
  if (Answers >= 1) {
    // pool.ntp.org CNAME x.ntp.org (name as pointer to the question)
    static const UINT8  Cname[] = {
      0xC0, 0x0C, 0x00, 0x05, 0x00, 0x01, 0, 0, 0, 60, 0x00, 0x04,
      1, 'x', 0xC0, 0x11
    };
    memcpy (&B[N], Cname, sizeof (Cname));
    N += sizeof (Cname);
  }

  for (int i = 1; i < Answers; i++) {
    UINT8  A[] = {
      0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0, 0, 0, 60, 0x00, 0x04,
      192, 0, 2, (UINT8)i
    };
    memcpy (&B[N], A, sizeof (A));
    N += sizeof (A);
  }

  return N;
}

static void
TestDnsReply (void)
{
  UINT8   B[512];
  UINT8   Addr[4 * 4];
  size_t  N;

  N = MakeDnsReply (B, 0x4242, 0, 4);
  CHECK (DnsParseReply (B, N, 0x4242, Addr, 4) == 3);
  CHECK (Addr[0] == 192 && Addr[3] == 1 && Addr[7] == 2 && Addr[11] == 3);
  CHECK (DnsParseReply (B, N, 0x4242, Addr, 1) == 1);
  CHECK (DnsParseReply (B, N, 0x4243, Addr, 4) == 0);     // not our ID
  CHECK (DnsParseReply (B, N - 2, 0x4242, Addr, 4) == 2);  // last record truncated
  CHECK (DnsParseReply (B, 20, 0x4242, Addr, 4) == 0);     // question truncated

  N = MakeDnsReply (B, 0x4242, 3, 0);                      // NXDOMAIN
  CHECK (DnsParseReply (B, N, 0x4242, Addr, 4) == 0);

  N    = MakeDnsReply (B, 0x4242, 0, 2);
  B[2] = 0x01;                                             // a query, not a response
  CHECK (DnsParseReply (B, N, 0x4242, Addr, 4) == 0);

  // A label length that runs past the end.
  N     = MakeDnsReply (B, 0x4242, 0, 2);
  B[12] = 60;
  CHECK (DnsParseReply (B, N, 0x4242, Addr, 4) == 0);

  // Answer count larger than what is there.
  N    = MakeDnsReply (B, 0x4242, 0, 2);
  B[7] = 200;
  CHECK (DnsParseReply (B, N, 0x4242, Addr, 4) == 1);

  // Every truncation of a valid reply must be safe.
  N = MakeDnsReply (B, 0x4242, 0, 4);
  for (size_t L = 0; L <= N; L++) {
    (void)DnsParseReply (B, L, 0x4242, Addr, 4);
  }
}

static void
TestTimeBase (void)
{
  EFI_TIME  T;

  EpochToEfiTime (0, &T);
  CHECK (T.Year == 1970 && T.Month == 1 && T.Day == 1 && T.Hour == 0);
  EpochToEfiTime (2085978496ULL, &T);           // era 1 starts
  CHECK (T.Year == 2036 && T.Month == 2 && T.Day == 7 && T.Hour == 6 && T.Minute == 28 && T.Second == 16);
  EpochToEfiTime (951782400ULL, &T);            // 2000-02-29
  CHECK (T.Year == 2000 && T.Month == 2 && T.Day == 29);
  EpochToEfiTime (4107542400ULL, &T);           // 2100-03-01 (2100 is not a leap year)
  CHECK (T.Year == 2100 && T.Month == 3 && T.Day == 1);
  CHECK (EfiTimeToEpoch (&T) == 4107542400ULL);
}

static UINT64
NowNs (void)
{
  struct timespec  Ts;

  clock_gettime (CLOCK_MONOTONIC, &Ts);
  return (UINT64)Ts.tv_sec * 1000000000ULL + (UINT64)Ts.tv_nsec;
}

static int
LiveNtp (const char *Ip)
{
  int                 S;
  struct sockaddr_in  To = { 0 };
  struct timeval      Tv = { 2, 0 };
  UINT8               Req[SNTP_PACKET_SIZE];
  UINT8               Rsp[512];
  SNTP_REPLY          R;
  UINT64              Nonce, T1, T4, UnixNs;
  struct timespec     Real;
  ssize_t             N;
  RETURN_STATUS       Status;

  S = socket (AF_INET, SOCK_DGRAM, 0);
  setsockopt (S, SOL_SOCKET, SO_RCVTIMEO, &Tv, sizeof (Tv));
  To.sin_family = AF_INET;
  To.sin_port   = htons (SNTP_PORT);
  inet_pton (AF_INET, Ip, &To.sin_addr);

  Nonce = ((UINT64)rand () << 33) ^ ((UINT64)rand () << 2) ^ NowNs () ^ 1;
  SntpBuildRequest (Req, Nonce);
  T1 = NowNs ();
  sendto (S, Req, sizeof (Req), 0, (struct sockaddr *)&To, sizeof (To));
  N  = recv (S, Rsp, sizeof (Rsp), 0);
  T4 = NowNs ();
  clock_gettime (CLOCK_REALTIME, &Real);
  close (S);
  if (N < 0) {
    printf ("ntp %s: no reply\n", Ip);
    return 1;
  }

  Status = SntpParseReply (Rsp, (UINTN)N, Nonce, &R);
  if (Status != RETURN_SUCCESS) {
    printf ("ntp %s: rejected (%llx)\n", Ip, (unsigned long long)Status);
    return 1;
  }

  UnixNs = SntpReplyTimeUnixNs (&R, T4 - T1, PIVOT_2026);
  printf (
    "ntp %s: stratum %u, rtt %.1f ms, hold %.3f ms, server-minus-host %+.3f ms\n",
    Ip,
    R.Stratum,
    (T4 - T1) / 1e6,
    SntpServerHoldNs (&R) / 1e6,
    ((double)(INT64)(UnixNs - ((UINT64)Real.tv_sec * 1000000000ULL + (UINT64)Real.tv_nsec))) / 1e6
    );

  // A nonce that does not match must be refused for the same bytes.
  CHECK (SntpParseReply (Rsp, (UINTN)N, Nonce ^ 1, &R) == RETURN_UNSUPPORTED);
  return mFailures != 0;
}

static int
LiveDns (const char *Ip, const char *Name)
{
  int                 S;
  struct sockaddr_in  To = { 0 };
  struct timeval      Tv = { 2, 0 };
  UINT8               Q[512], A[1500], Addr[8 * 4];
  UINTN               Len, Count;
  ssize_t             N;
  UINT16              Id = (UINT16)(NowNs () | 1);

  Len = DnsBuildQuery (Q, sizeof (Q), Id, Name);
  if (Len == 0) {
    printf ("dns: bad name\n");
    return 1;
  }

  S = socket (AF_INET, SOCK_DGRAM, 0);
  setsockopt (S, SOL_SOCKET, SO_RCVTIMEO, &Tv, sizeof (Tv));
  To.sin_family = AF_INET;
  To.sin_port   = htons (DNS_PORT);
  inet_pton (AF_INET, Ip, &To.sin_addr);
  sendto (S, Q, Len, 0, (struct sockaddr *)&To, sizeof (To));
  N = recv (S, A, sizeof (A), 0);
  close (S);
  if (N < 0) {
    printf ("dns %s: no reply\n", Ip);
    return 1;
  }

  Count = DnsParseReply (A, (UINTN)N, Id, Addr, 8);
  printf ("dns %s %s:", Ip, Name);
  for (UINTN i = 0; i < Count; i++) {
    printf (" %u.%u.%u.%u", Addr[i * 4], Addr[i * 4 + 1], Addr[i * 4 + 2], Addr[i * 4 + 3]);
  }

  printf ("\n");
  return Count == 0;
}

static int
EpochFromStdin (void)
{
  unsigned long long  E;
  EFI_TIME            T;

  while (scanf ("%llu", &E) == 1) {
    EpochToEfiTime ((UINTN)E, &T);
    printf (
      "%llu %04u-%02u-%02u %02u:%02u:%02u %llu\n",
      E, T.Year, T.Month, T.Day, T.Hour, T.Minute, T.Second,
      (unsigned long long)EfiTimeToEpoch (&T)
      );
  }

  return 0;
}

int
main (int argc, char **argv)
{
  if ((argc == 3) && (strcmp (argv[1], "ntp") == 0)) {
    return LiveNtp (argv[2]);
  }

  if ((argc == 4) && (strcmp (argv[1], "dns") == 0)) {
    return LiveDns (argv[2], argv[3]);
  }

  if ((argc == 2) && (strcmp (argv[1], "epoch") == 0)) {
    return EpochFromStdin ();
  }

  TestRequest ();
  TestReply ();
  TestEra ();
  TestDelay ();
  TestDnsQuery ();
  TestDnsReply ();
  TestTimeBase ();
  printf ("%s (%d failure(s))\n", mFailures ? "FAILED" : "all passed", mFailures);
  return mFailures != 0;
}

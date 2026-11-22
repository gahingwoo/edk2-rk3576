/** @file
 *
 *  Set the firmware clock from the network (SNTP, RFC 4330).
 *
 *  None of the RK3576 boards keeps time in the firmware: the clock is
 *  EmbeddedPkg's virtual RTC, which stores Unix seconds in a variable and adds
 *  the uptime.  Without a SetTime() it starts every boot from the last value
 *  stored, or from the build date, and an OS that reads it at boot (Windows
 *  does) is hours or months off.
 *
 *  This driver does one SNTP exchange per boot, in the background:
 *
 *    1. When the boot manager has connected devices (the event
 *       gRockchipEventPlatformBmDevicesConnectedGuid), it looks for a NIC.
 *       If the boot discovery policy did not connect any, it connects the
 *       SNP handles itself, and disconnects what it added when it is done.
 *    2. Address: whatever the NIC's IP4 configuration already provides.  A
 *       static address is used as is; under the DHCP policy it waits for the
 *       lease IP4 is getting.  Only when the policy is static and no address
 *       is set (the state of a NIC nobody configured) does it run DHCP of its
 *       own, on a DHCP4 child, so the stored IP4 policy is never changed.
 *    3. Servers, in order: NTP servers from DHCP option 42; pool.ntp.org and
 *       time.cloudflare.com through the DNS servers the address came with;
 *       then two fixed anycast addresses (see mFallbackServers).
 *    4. A reply is used only if it passes SntpParseReply() and lands after
 *       the build date; then SetTime() with the UTC time.
 *
 *  Everything runs from a 50 ms timer and protocol completion events at
 *  TPL_CALLBACK; nothing waits.  Two budgets bound it: NTS_ADDRESS_BUDGET_MS
 *  from the start for a link and an address, then NTS_QUERY_BUDGET_MS from
 *  the address for the queries, which go through the server list up to
 *  NTS_ROUNDS times.  It is abandoned when a budget runs out or at
 *  ExitBootServices, whichever comes first, with one line saying which
 *  stage it stopped at and what it saw.
 *
 *  The address budget is long on purpose.  Under the DHCP policy the lease
 *  is IP4's, and EDK2's Dhcp4 with no callback keeps collecting offers
 *  until the first DISCOVER times out (mDhcp4DefaultTimeout {4, 8, 16, 32}
 *  s, Dhcp4Io.c DhcpOnTimerTick "select offer at each timeout"), so a lease
 *  takes 4 s at best and 12 s when the first DISCOVER went out before the
 *  PHY had link.  The first version gave the whole attempt 8 s from the
 *  start: on the TaishanPi 3M, with the cable in, that ended "gave up after
 *  8001 ms (no server answered)" on two boots in a row.  Nothing here holds
 *  up the boot, so waiting longer only costs a timer tick every 50 ms.
 *
 *  Copyright (c) 2026, gahingwoo <gahing@gahingwoo.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/NetLib.h>
#include <Library/PcdLib.h>
#include <Library/TimeBaseLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/Dhcp4.h>
#include <Protocol/Ip4Config2.h>
#include <Protocol/ServiceBinding.h>
#include <Protocol/SimpleNetwork.h>
#include <Protocol/Udp4.h>

#include "SntpPacket.h"

#define NTS_TICK_MS             50
#define NTS_ADDRESS_BUDGET_MS   30000   // start to address: link + IP4's DHCP
#define NTS_QUERY_BUDGET_MS     10000   // address to answer
#define NTS_ROUNDS              2       // passes over the server list
#define NTS_QUERY_TIMEOUT_MS    1000
#define NTS_MAX_CANDIDATES    16
#define NTS_MAX_DNS           3
#define NTS_MAX_NICS          4
#define NTS_ADDRS_PER_NAME    2
#define NTS_BUFFER_SIZE       512

#define NS_PER_MS  1000000ULL

//
// Latest year accepted, counted from the build year.  The NTP era pivot
// (NtsBuildYearUnix) already rules out anything before the build; this
// rules out the other way an era guess goes wrong: a server stuck in 1990
// reads as era 1, 2126.
//
#define NTS_MAX_YEARS_AFTER_BUILD  50

#define DHCP4_TAG_NETMASK_    1
#define DHCP4_TAG_ROUTER_     3
#define DHCP4_TAG_DNS_        6
#define DHCP4_TAG_NTP_        42
#define DHCP4_TAG_PARA_LIST_  55

typedef enum {
  NtsIdle,
  NtsWaitNic,
  NtsWaitAddress,
  NtsDhcp,
  NtsQuery,
  NtsFinishing,
  NtsDone
} NTS_STATE;

typedef enum {
  NtsServerAddress,
  NtsServerName
} NTS_SERVER_KIND;

typedef struct {
  NTS_SERVER_KIND     Kind;
  EFI_IPv4_ADDRESS    Address;    // NtsServerAddress
  CONST CHAR8         *Name;      // NtsServerName; for an address, where it came from
} NTS_SERVER;

typedef struct {
  NTS_STATE                    State;
  EFI_HANDLE                   ImageHandle;
  EFI_EVENT                    Timer;
  UINT64                       StartNs;
  UINT64                       QueryStartNs;
  CONST CHAR8                  *Problem;    // why it would give up now

  //
  // What the attempt reached, for the line it ends with.  -1: never.
  //
  INT64                        LinkMs;
  INT64                        AddressMs;
  UINT32                       Round;
  UINT32                       Sent;        // Transmit() accepted
  UINT32                       TxDone;      // left the NIC (ARP resolved)
  UINT32                       TxFailed;    // cancelled or failed, e.g. no ARP answer
  UINT32                       Datagrams;   // anything received on our port
  UINT32                       DnsAnswers;
  UINT32                       NtpReplies;
  EFI_STATUS                   LastTxStatus; // of the last failed transmit token

  //
  // NICs this driver connected itself, and the drivers that managed each
  // before, so that exactly what was added can be disconnected again.
  //
  UINTN                        ConnectedCount;
  EFI_HANDLE                   Connected[NTS_MAX_NICS];
  EFI_HANDLE                   *DriversBefore[NTS_MAX_NICS];
  UINTN                        DriversBeforeCount[NTS_MAX_NICS];

  EFI_HANDLE                   Nic;
  EFI_IP4_CONFIG2_PROTOCOL     *Ip4Config2;

  EFI_HANDLE                   Dhcp4Child;
  EFI_DHCP4_PROTOCOL           *Dhcp4;
  EFI_EVENT                    Dhcp4Event;
  BOOLEAN                      Dhcp4Completed;

  BOOLEAN                      UseDefaultAddress;
  EFI_IPv4_ADDRESS             Station;
  EFI_IPv4_ADDRESS             Netmask;
  EFI_IPv4_ADDRESS             Gateway;
  EFI_IPv4_ADDRESS             Dns[NTS_MAX_DNS];
  UINTN                        DnsCount;

  EFI_HANDLE                   Udp4Child;
  EFI_UDP4_PROTOCOL            *Udp4;
  EFI_UDP4_COMPLETION_TOKEN    RxToken;
  EFI_UDP4_COMPLETION_TOKEN    TxToken;
  BOOLEAN                      RxPosted;
  BOOLEAN                      TxPending;
  EFI_UDP4_SESSION_DATA        TxSession;
  EFI_UDP4_TRANSMIT_DATA       TxData;
  UINT8                        TxBuffer[NTS_BUFFER_SIZE];
  UINT8                        RxBuffer[NTS_BUFFER_SIZE];

  NTS_SERVER                   Servers[NTS_MAX_CANDIDATES];
  UINTN                        ServerCount;
  UINTN                        ServerIndex;
  UINTN                        DnsIndex;

  BOOLEAN                      QueryOutstanding;
  UINT64                       QuerySentNs;
  EFI_IPv4_ADDRESS             QueryTo;
  UINT16                       QueryPort;
  UINT64                       Nonce;
  UINT16                       DnsId;
} NTS_CONTEXT;

STATIC NTS_CONTEXT  mNts;

STATIC CONST CHAR8  *mServerNames[] = {
  "pool.ntp.org",
  "time.cloudflare.com",
};

//
// Used when DHCP gave no NTP server and no name could be resolved (no DNS
// server, or DNS blocked).  Both are anycast addresses their operators
// publish as the fixed address of a public NTP service, so they stay valid
// without DNS and are served from a nearby site:
//
//   162.159.200.1  time.cloudflare.com (Cloudflare; also 162.159.200.123)
//   216.239.35.0   time.google.com     (Google Public NTP; .4 .8 .12 too)
//
// Two operators, so one outage or one blocked prefix does not lose both.
// Google smears leap seconds over a day; that is far below the one-second
// resolution of the virtual RTC this driver sets.
//
STATIC CONST UINT8  mFallbackServers[][4] = {
  { 162, 159, 200, 1 },
  { 216, 239, 35,  0 },
};

STATIC
UINT64
NtsNowNs (
  VOID
  )
{
  return GetTimeInNanoSecond (GetPerformanceCounter ());
}

STATIC
UINT64
NtsElapsedMs (
  VOID
  )
{
  return (NtsNowNs () - mNts.StartNs) / NS_PER_MS;
}

/**
  64 random bits, never 0: the NTP nonce and the DNS query ID.  The counter
  is mixed in so that a missing RNG still gives a value that changes.
**/
STATIC
UINT64
NtsRandom64 (
  VOID
  )
{
  UINT64  Value;

  Value = 0;
  PseudoRandom (&Value, sizeof (Value));
  Value ^= GetPerformanceCounter ();
  return (Value != 0) ? Value : 1;
}

/**
  Start of the build year, Unix seconds: no honest server is earlier, and
  it is the pivot for the NTP era.
**/
STATIC
UINT64
NtsBuildYearUnix (
  VOID
  )
{
  EFI_TIME  Time;

  ZeroMem (&Time, sizeof (Time));
  Time.Year  = TIME_BUILD_YEAR;
  Time.Month = 1;
  Time.Day   = 1;
  return EfiTimeToEpoch (&Time);
}

/**
  An EFI_TIME as UTC Unix seconds, undoing its time zone and daylight.
**/
STATIC
INT64
NtsTimeToUtc (
  IN CONST EFI_TIME  *Time
  )
{
  INT64  Seconds;

  Seconds = (INT64)EfiTimeToEpoch ((EFI_TIME *)Time);
  if (Time->TimeZone != EFI_UNSPECIFIED_TIMEZONE) {
    Seconds -= (INT64)Time->TimeZone * 60;
  }

  if ((Time->Daylight & EFI_TIME_IN_DAYLIGHT) != 0) {
    Seconds -= 3600;
  }

  return Seconds;
}

STATIC
VOID
NtsFinish (
  IN CONST CHAR8  *Problem
  );

// ---------------------------------------------------------------------------
// Connecting and disconnecting NICs
// ---------------------------------------------------------------------------

/**
  The drivers that manage Handle: agents of a BY_DRIVER open on any of its
  protocols.  Caller frees *List.
**/
STATIC
VOID
NtsManagingDrivers (
  IN  EFI_HANDLE  Handle,
  OUT EFI_HANDLE  **List,
  OUT UINTN       *Count
  )
{
  EFI_STATUS                           Status;
  EFI_GUID                             **Guids;
  UINTN                                GuidCount;
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY  *Info;
  UINTN                                InfoCount;
  UINTN                                G;
  UINTN                                I;
  UINTN                                K;
  EFI_HANDLE                           *Result;
  UINTN                                ResultCount;
  UINTN                                ResultMax;

  *List  = NULL;
  *Count = 0;

  Status = gBS->ProtocolsPerHandle (Handle, &Guids, &GuidCount);
  if (EFI_ERROR (Status)) {
    return;
  }

  ResultMax   = 0;
  ResultCount = 0;
  Result      = NULL;
  for (G = 0; G < GuidCount; G++) {
    Status = gBS->OpenProtocolInformation (Handle, Guids[G], &Info, &InfoCount);
    if (EFI_ERROR (Status)) {
      continue;
    }

    for (I = 0; I < InfoCount; I++) {
      if ((Info[I].Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) == 0) {
        continue;
      }

      for (K = 0; K < ResultCount; K++) {
        if (Result[K] == Info[I].AgentHandle) {
          break;
        }
      }

      if (K < ResultCount) {
        continue;
      }

      if (ResultCount == ResultMax) {
        Result = ReallocatePool (
                   ResultMax * sizeof (EFI_HANDLE),
                   (ResultMax + 8) * sizeof (EFI_HANDLE),
                   Result
                   );
        if (Result == NULL) {
          FreePool (Info);
          FreePool (Guids);
          return;
        }

        ResultMax += 8;
      }

      Result[ResultCount++] = Info[I].AgentHandle;
    }

    FreePool (Info);
  }

  FreePool (Guids);
  *List  = Result;
  *Count = ResultCount;
}

STATIC
BOOLEAN
NtsHandleInList (
  IN EFI_HANDLE  Handle,
  IN EFI_HANDLE  *List,
  IN UINTN       Count
  )
{
  UINTN  Index;

  for (Index = 0; Index < Count; Index++) {
    if (List[Index] == Handle) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  The boot discovery policy did not connect the network: connect every SNP
  handle, remembering who managed it before.
**/
STATIC
VOID
NtsConnectNics (
  IN EFI_HANDLE  *SnpHandles,
  IN UINTN       SnpCount
  )
{
  UINTN  Index;
  UINTN  Slot;

  DEBUG ((DEBUG_INFO, "NetworkTimeSync: connecting %u network interface(s) for the time sync\n", (UINT32)SnpCount));
  for (Index = 0; Index < SnpCount && mNts.ConnectedCount < NTS_MAX_NICS; Index++) {
    Slot = mNts.ConnectedCount++;
    mNts.Connected[Slot] = SnpHandles[Index];
    NtsManagingDrivers (
      SnpHandles[Index],
      &mNts.DriversBefore[Slot],
      &mNts.DriversBeforeCount[Slot]
      );
    gBS->ConnectController (SnpHandles[Index], NULL, NULL, TRUE);
  }
}

/**
  Disconnect every driver NtsConnectNics() added.  Two passes, because a
  driver whose children are still open by another one refuses on the first.
**/
STATIC
VOID
NtsDisconnectNics (
  VOID
  )
{
  UINTN       Slot;
  UINTN       Pass;
  EFI_HANDLE  *Now;
  UINTN       NowCount;
  UINTN       Index;

  for (Slot = 0; Slot < mNts.ConnectedCount; Slot++) {
    for (Pass = 0; Pass < 2; Pass++) {
      NtsManagingDrivers (mNts.Connected[Slot], &Now, &NowCount);
      for (Index = 0; Index < NowCount; Index++) {
        if (!NtsHandleInList (Now[Index], mNts.DriversBefore[Slot], mNts.DriversBeforeCount[Slot])) {
          gBS->DisconnectController (mNts.Connected[Slot], Now[Index], NULL);
        }
      }

      if (Now != NULL) {
        FreePool (Now);
      }
    }

    if (mNts.DriversBefore[Slot] != NULL) {
      FreePool (mNts.DriversBefore[Slot]);
      mNts.DriversBefore[Slot] = NULL;
    }
  }

  mNts.ConnectedCount = 0;
}

// ---------------------------------------------------------------------------
// Server list
// ---------------------------------------------------------------------------

STATIC
VOID
NtsAddAddress (
  IN CONST UINT8  *Address,
  IN CONST CHAR8  *From
  )
{
  UINTN  Index;

  if ((mNts.ServerCount >= NTS_MAX_CANDIDATES) ||
      ((Address[0] | Address[1] | Address[2] | Address[3]) == 0))
  {
    return;
  }

  for (Index = 0; Index < mNts.ServerCount; Index++) {
    if ((mNts.Servers[Index].Kind == NtsServerAddress) &&
        (CompareMem (&mNts.Servers[Index].Address, Address, 4) == 0))
    {
      return;
    }
  }

  mNts.Servers[mNts.ServerCount].Kind = NtsServerAddress;
  CopyMem (&mNts.Servers[mNts.ServerCount].Address, Address, 4);
  mNts.Servers[mNts.ServerCount].Name = From;
  mNts.ServerCount++;
}

STATIC
VOID
NtsAddDns (
  IN CONST UINT8  *Address
  )
{
  UINTN  Index;

  if ((mNts.DnsCount >= NTS_MAX_DNS) || ((Address[0] | Address[1] | Address[2] | Address[3]) == 0)) {
    return;
  }

  for (Index = 0; Index < mNts.DnsCount; Index++) {
    if (CompareMem (&mNts.Dns[Index], Address, 4) == 0) {
      return;
    }
  }

  CopyMem (&mNts.Dns[mNts.DnsCount++], Address, 4);
}

/**
  Take DNS (option 6) and NTP (option 42) servers out of a DHCP reply.
**/
STATIC
VOID
NtsHarvestDhcpOptions (
  IN EFI_DHCP4_PROTOCOL  *Dhcp4,
  IN EFI_DHCP4_PACKET    *Packet
  )
{
  EFI_STATUS               Status;
  UINT32                   Count;
  EFI_DHCP4_PACKET_OPTION  **Options;
  UINT32                   Index;
  UINT32                   Offset;

  if (Packet == NULL) {
    return;
  }

  Count  = 0;
  Status = Dhcp4->Parse (Dhcp4, Packet, &Count, NULL);
  if ((Status != EFI_BUFFER_TOO_SMALL) || (Count == 0)) {
    return;
  }

  Options = AllocateZeroPool (Count * sizeof (EFI_DHCP4_PACKET_OPTION *));
  if (Options == NULL) {
    return;
  }

  Status = Dhcp4->Parse (Dhcp4, Packet, &Count, Options);
  if (!EFI_ERROR (Status)) {
    for (Index = 0; Index < Count; Index++) {
      for (Offset = 0; Offset + 4 <= Options[Index]->Length; Offset += 4) {
        if (Options[Index]->OpCode == DHCP4_TAG_NTP_) {
          NtsAddAddress (&Options[Index]->Data[Offset], "DHCP");
        } else if (Options[Index]->OpCode == DHCP4_TAG_DNS_) {
          NtsAddDns (&Options[Index]->Data[Offset]);
        }
      }
    }
  }

  FreePool (Options);
}

/**
  Under the DHCP policy the lease belongs to IP4's own DHCP4 child.  It did
  not ask for option 42, but a server may send it anyway; look.
**/
STATIC
VOID
NtsHarvestForeignLease (
  VOID
  )
{
  EFI_STATUS           Status;
  EFI_HANDLE           *Handles;
  UINTN                Count;
  UINTN                Index;
  EFI_DHCP4_PROTOCOL   *Dhcp4;
  EFI_DHCP4_MODE_DATA  Mode;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiDhcp4ProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiDhcp4ProtocolGuid, (VOID **)&Dhcp4);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = Dhcp4->GetModeData (Dhcp4, &Mode);
    if (!EFI_ERROR (Status) && (Mode.State == Dhcp4Bound) &&
        EFI_IP4_EQUAL (&Mode.ClientAddress, &mNts.Station))
    {
      NtsHarvestDhcpOptions (Dhcp4, Mode.ReplyPacket);
    }
  }

  FreePool (Handles);
}

STATIC
VOID
NtsBuildServerList (
  VOID
  )
{
  UINTN  Index;

  // DHCP option 42 entries are already in the list.
  for (Index = 0; Index < ARRAY_SIZE (mServerNames); Index++) {
    if ((mNts.DnsCount > 0) && (mNts.ServerCount < NTS_MAX_CANDIDATES)) {
      mNts.Servers[mNts.ServerCount].Kind = NtsServerName;
      mNts.Servers[mNts.ServerCount].Name = mServerNames[Index];
      mNts.ServerCount++;
    }
  }

  for (Index = 0; Index < ARRAY_SIZE (mFallbackServers); Index++) {
    NtsAddAddress (mFallbackServers[Index], "fixed");
  }
}

// ---------------------------------------------------------------------------
// UDP
// ---------------------------------------------------------------------------

STATIC
VOID
NtsHandleDatagram (
  IN CONST UINT8             *Data,
  IN UINTN                   Length,
  IN CONST EFI_IPv4_ADDRESS  *From,
  IN UINT16                  FromPort
  );

STATIC
VOID
NtsPostReceive (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mNts.RxPosted || (mNts.Udp4 == NULL) || (mNts.State != NtsQuery)) {
    return;
  }

  mNts.RxToken.Status      = EFI_NOT_READY;
  mNts.RxToken.Packet.RxData = NULL;
  Status                   = mNts.Udp4->Receive (mNts.Udp4, &mNts.RxToken);
  if (!EFI_ERROR (Status)) {
    mNts.RxPosted = TRUE;
  }
}

STATIC
VOID
EFIAPI
NtsOnReceive (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_UDP4_RECEIVE_DATA  *Rx;
  UINTN                  Length;
  UINTN                  Index;
  UINTN                  Chunk;
  EFI_IPv4_ADDRESS       From;
  UINT16                 FromPort;

  mNts.RxPosted = FALSE;
  Rx            = mNts.RxToken.Packet.RxData;

  if (EFI_ERROR (mNts.RxToken.Status) || (Rx == NULL)) {
    //
    // ICMP errors arrive here too.  Keep listening unless this is the
    // cancel from the cleanup.
    //
    if (mNts.RxToken.Status != EFI_ABORTED) {
      NtsPostReceive ();
    }

    return;
  }

  Length = 0;
  for (Index = 0; Index < Rx->FragmentCount; Index++) {
    Chunk = MIN (Rx->FragmentTable[Index].FragmentLength, sizeof (mNts.RxBuffer) - Length);
    CopyMem (&mNts.RxBuffer[Length], Rx->FragmentTable[Index].FragmentBuffer, Chunk);
    Length += Chunk;
  }

  mNts.Datagrams++;
  CopyMem (&From, &Rx->UdpSession.SourceAddress, sizeof (From));
  FromPort = Rx->UdpSession.SourcePort;
  gBS->SignalEvent (Rx->RecycleSignal);

  if (mNts.State == NtsQuery) {
    NtsHandleDatagram (mNts.RxBuffer, Length, &From, FromPort);
  }

  NtsPostReceive ();
}

STATIC
VOID
EFIAPI
NtsOnTransmit (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mNts.TxPending = FALSE;
  if (EFI_ERROR (mNts.TxToken.Status)) {
    mNts.TxFailed++;
    mNts.LastTxStatus = mNts.TxToken.Status;
  } else {
    mNts.TxDone++;
  }
}

STATIC
EFI_STATUS
NtsStartUdp (
  VOID
  )
{
  EFI_STATUS            Status;
  EFI_UDP4_CONFIG_DATA  Config;
  EFI_IPv4_ADDRESS      Zero;

  Status = NetLibCreateServiceChild (
             mNts.Nic,
             mNts.ImageHandle,
             &gEfiUdp4ServiceBindingProtocolGuid,
             &mNts.Udp4Child
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (mNts.Udp4Child, &gEfiUdp4ProtocolGuid, (VOID **)&mNts.Udp4);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (&Config, sizeof (Config));
  Config.TimeToLive        = 64;
  Config.DoNotFragment     = TRUE;
  Config.UseDefaultAddress = mNts.UseDefaultAddress;
  if (!mNts.UseDefaultAddress) {
    CopyMem (&Config.StationAddress, &mNts.Station, sizeof (EFI_IPv4_ADDRESS));
    CopyMem (&Config.SubnetMask, &mNts.Netmask, sizeof (EFI_IPv4_ADDRESS));
  }

  //
  // Port 0: an ephemeral source port.  No remote address, so one child can
  // talk to DNS and NTP servers alike, each datagram naming its destination.
  //
  Status = mNts.Udp4->Configure (mNts.Udp4, &Config);
  if (EFI_ERROR (Status) && (Status != EFI_NO_MAPPING)) {
    return Status;
  }

  if (!mNts.UseDefaultAddress && (EFI_IP4 (mNts.Gateway) != 0)) {
    ZeroMem (&Zero, sizeof (Zero));
    mNts.Udp4->Routes (mNts.Udp4, FALSE, &Zero, &Zero, &mNts.Gateway);
  }

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, NtsOnReceive, NULL, &mNts.RxToken.Event);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, NtsOnTransmit, NULL, &mNts.TxToken.Event);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mNts.State        = NtsQuery;
  mNts.QueryStartNs = NtsNowNs ();
  mNts.AddressMs    = (INT64)NtsElapsedMs ();
  NtsPostReceive ();
  return EFI_SUCCESS;
}

STATIC
VOID
NtsStopUdp (
  VOID
  )
{
  if (mNts.Udp4 != NULL) {
    mNts.Udp4->Cancel (mNts.Udp4, NULL);
    mNts.Udp4->Configure (mNts.Udp4, NULL);
    mNts.Udp4 = NULL;
  }

  if (mNts.Udp4Child != NULL) {
    NetLibDestroyServiceChild (mNts.Nic, mNts.ImageHandle, &gEfiUdp4ServiceBindingProtocolGuid, mNts.Udp4Child);
    mNts.Udp4Child = NULL;
  }

  if (mNts.RxToken.Event != NULL) {
    gBS->CloseEvent (mNts.RxToken.Event);
    mNts.RxToken.Event = NULL;
  }

  if (mNts.TxToken.Event != NULL) {
    gBS->CloseEvent (mNts.TxToken.Event);
    mNts.TxToken.Event = NULL;
  }

  mNts.RxPosted  = FALSE;
  mNts.TxPending = FALSE;
}

STATIC
EFI_STATUS
NtsTransmit (
  IN CONST EFI_IPv4_ADDRESS  *To,
  IN UINT16                  Port,
  IN UINTN                   Length
  )
{
  EFI_STATUS  Status;

  ZeroMem (&mNts.TxSession, sizeof (mNts.TxSession));
  CopyMem (&mNts.TxSession.DestinationAddress, To, sizeof (EFI_IPv4_ADDRESS));
  mNts.TxSession.DestinationPort = Port;

  ZeroMem (&mNts.TxData, sizeof (mNts.TxData));
  mNts.TxData.UdpSessionData                 = &mNts.TxSession;
  mNts.TxData.DataLength                     = (UINT32)Length;
  mNts.TxData.FragmentCount                  = 1;
  mNts.TxData.FragmentTable[0].FragmentLength = (UINT32)Length;
  mNts.TxData.FragmentTable[0].FragmentBuffer = mNts.TxBuffer;

  mNts.TxToken.Status        = EFI_NOT_READY;
  mNts.TxToken.Packet.TxData = &mNts.TxData;

  mNts.TxPending = TRUE;
  Status         = mNts.Udp4->Transmit (mNts.Udp4, &mNts.TxToken);
  if (EFI_ERROR (Status)) {
    mNts.TxPending = FALSE;
  }

  return Status;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

STATIC
VOID
NtsSetClock (
  IN CONST SNTP_REPLY  *Reply
  )
{
  EFI_STATUS  Status;
  UINT64      ArrivedNs;
  UINT64      UnixNs;
  UINT64      Pivot;
  EFI_TIME    Old;
  EFI_TIME    New;
  INT64       OldUtc;
  INT64       Offset;
  CONST CHAR8  *Source;

  ArrivedNs = NtsNowNs ();
  Pivot     = NtsBuildYearUnix ();
  UnixNs    = SntpReplyTimeUnixNs (Reply, ArrivedNs - mNts.QuerySentNs, Pivot);

  Status = gRT->GetTime (&Old, NULL);
  if (EFI_ERROR (Status)) {
    ZeroMem (&Old, sizeof (Old));
    Old.Year  = 1970;
    Old.Month = 1;
    Old.Day   = 1;
    Old.TimeZone = EFI_UNSPECIFIED_TIMEZONE;
  }

  //
  // The time when SetTime() runs, not when the reply came in.
  //
  UnixNs += NtsNowNs () - ArrivedNs;

  ZeroMem (&New, sizeof (New));
  EpochToEfiTime ((UINTN)(UnixNs / SNTP_NS_PER_SEC), &New);
  New.Nanosecond = (UINT32)(UnixNs % SNTP_NS_PER_SEC);
  New.TimeZone   = EFI_UNSPECIFIED_TIMEZONE;
  New.Daylight   = 0;

  Source = mNts.Servers[mNts.ServerIndex].Name;

  if ((New.Year < TIME_BUILD_YEAR) || (New.Year > TIME_BUILD_YEAR + NTS_MAX_YEARS_AFTER_BUILD) ||
      !IsTimeValid (&New))
  {
    DEBUG ((
      DEBUG_INFO,
      "NetworkTimeSync: ignored %d.%d.%d.%d (%a): implausible time %04u-%02u-%02u\n",
      mNts.QueryTo.Addr[0], mNts.QueryTo.Addr[1], mNts.QueryTo.Addr[2], mNts.QueryTo.Addr[3],
      Source, New.Year, New.Month, New.Day
      ));
    return;
  }

  OldUtc = NtsTimeToUtc (&Old);
  Offset = (INT64)(UnixNs / SNTP_NS_PER_SEC) - OldUtc;

  //
  // Every SetTime() is three variable writes in the virtual RTC; skip it
  // when the clock is already right and already in UTC.
  //
  if ((Offset > -2) && (Offset < 2) && (Old.TimeZone == EFI_UNSPECIFIED_TIMEZONE) &&
      ((Old.Daylight & EFI_TIME_IN_DAYLIGHT) == 0))
  {
    Status = EFI_SUCCESS;
  } else {
    Status = gRT->SetTime (&New);
  }

  DEBUG ((
    DEBUG_INFO,
    "NetworkTimeSync: %04u-%02u-%02u %02u:%02u:%02u -> %04u-%02u-%02u %02u:%02u:%02u UTC "
    "(%ld s) from %d.%d.%d.%d (%a), stratum %u, after %lu ms\n",
    Old.Year, Old.Month, Old.Day, Old.Hour, Old.Minute, Old.Second,
    New.Year, New.Month, New.Day, New.Hour, New.Minute, New.Second,
    Offset,
    mNts.QueryTo.Addr[0], mNts.QueryTo.Addr[1], mNts.QueryTo.Addr[2], mNts.QueryTo.Addr[3],
    Source, Reply->Stratum, NtsElapsedMs ()
    ));
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "NetworkTimeSync: SetTime: %r\n", Status));
  }

  NtsFinish (NULL);
}

/**
  The current server failed or timed out: move on.  For a name, the next
  DNS server is tried first.
**/
STATIC
VOID
NtsAdvance (
  IN BOOLEAN  NameUnresolvable
  )
{
  mNts.QueryOutstanding = FALSE;
  if (mNts.ServerIndex >= mNts.ServerCount) {
    return;
  }

  if ((mNts.Servers[mNts.ServerIndex].Kind == NtsServerName) && !NameUnresolvable) {
    mNts.DnsIndex++;
    if (mNts.DnsIndex < mNts.DnsCount) {
      return;
    }
  }

  mNts.DnsIndex = 0;
  mNts.ServerIndex++;
}

/**
  A name resolved: its addresses take its place in the list.
**/
STATIC
VOID
NtsReplaceName (
  IN CONST UINT8  *Addresses,
  IN UINTN        Count
  )
{
  CONST CHAR8  *Name;
  UINTN        Index;
  UINTN        Slot;
  UINTN        Insert;
  UINTN        Dup;

  Name   = mNts.Servers[mNts.ServerIndex].Name;
  Insert = 0;
  for (Index = 0; Index < Count; Index++) {
    for (Dup = 0; Dup < mNts.ServerCount; Dup++) {
      if ((mNts.Servers[Dup].Kind == NtsServerAddress) &&
          (CompareMem (&mNts.Servers[Dup].Address, &Addresses[Index * 4], 4) == 0))
      {
        break;
      }
    }

    if (Dup < mNts.ServerCount) {
      continue;
    }

    if (Insert == 0) {
      Slot = mNts.ServerIndex;      // overwrite the name itself
    } else {
      if (mNts.ServerCount >= NTS_MAX_CANDIDATES) {
        break;
      }

      Slot = mNts.ServerIndex + Insert;
      CopyMem (
        &mNts.Servers[Slot + 1],
        &mNts.Servers[Slot],
        (mNts.ServerCount - Slot) * sizeof (NTS_SERVER)
        );
      mNts.ServerCount++;
    }

    mNts.Servers[Slot].Kind = NtsServerAddress;
    CopyMem (&mNts.Servers[Slot].Address, &Addresses[Index * 4], 4);
    mNts.Servers[Slot].Name = Name;
    Insert++;
  }

  mNts.QueryOutstanding = FALSE;
  mNts.DnsIndex         = 0;
  if (Insert == 0) {
    mNts.ServerIndex++;           // every address was already in the list
  }
}

STATIC
VOID
NtsHandleDatagram (
  IN CONST UINT8             *Data,
  IN UINTN                   Length,
  IN CONST EFI_IPv4_ADDRESS  *From,
  IN UINT16                  FromPort
  )
{
  RETURN_STATUS  Status;
  SNTP_REPLY     Reply;
  UINT8          Addresses[NTS_ADDRS_PER_NAME * 4];
  UINTN          Count;

  if (!mNts.QueryOutstanding || (FromPort != mNts.QueryPort) || !EFI_IP4_EQUAL (From, &mNts.QueryTo)) {
    return;
  }

  if (mNts.QueryPort == DNS_PORT) {
    mNts.DnsAnswers++;
    Count = DnsParseReply (Data, Length, mNts.DnsId, Addresses, NTS_ADDRS_PER_NAME);
    DEBUG ((
      DEBUG_VERBOSE,
      "NetworkTimeSync: DNS answer for %a: %u address(es), rcode %u\n",
      mNts.Servers[mNts.ServerIndex].Name,
      (UINT32)Count,
      (Length >= 4) ? (Data[3] & 0x0F) : 0xFF
      ));
    if (Count == 0) {
      //
      // An answer without an address (NXDOMAIN, or an ID that is not ours
      // -- then the real answer may still come, so only a valid header
      // counts as final).
      //
      if ((Length >= 2) && (((UINT16)Data[0] << 8 | Data[1]) == mNts.DnsId)) {
        NtsAdvance (TRUE);
      }

      return;
    }

    NtsReplaceName (Addresses, Count);
    return;
  }

  mNts.NtpReplies++;
  Status = SntpParseReply (Data, Length, mNts.Nonce, &Reply);
  DEBUG ((DEBUG_VERBOSE, "NetworkTimeSync: NTP reply of %u bytes: %r\n", (UINT32)Length, Status));
  if (Status == RETURN_UNSUPPORTED) {
    return;                       // not an answer to our request
  }

  if (RETURN_ERROR (Status)) {
    NtsAdvance (FALSE);           // kiss-o'-death or unsynchronised server
    return;
  }

  NtsSetClock (&Reply);
  if (mNts.State == NtsQuery) {
    NtsAdvance (FALSE);           // the time was implausible
  }
}

STATIC
VOID
NtsSendNext (
  VOID
  )
{
  EFI_STATUS  Status;
  NTS_SERVER  *Server;
  UINTN       Length;

  for ( ; ;) {
    if (mNts.ServerIndex >= mNts.ServerCount) {
      //
      // The whole list failed.  Go round again while there is time: the
      // first pass may have been spent on ARP, or on servers asked before
      // the uplink passed traffic.
      //
      if ((mNts.Round + 1 >= NTS_ROUNDS) || (mNts.ServerCount == 0)) {
        break;
      }

      mNts.Round++;
      mNts.ServerIndex = 0;
      mNts.DnsIndex    = 0;
    }

    Server = &mNts.Servers[mNts.ServerIndex];
    if (Server->Kind == NtsServerName) {
      if (mNts.DnsIndex >= mNts.DnsCount) {
        NtsAdvance (TRUE);
        continue;
      }

      mNts.DnsId = (UINT16)NtsRandom64 ();
      Length     = DnsBuildQuery (mNts.TxBuffer, sizeof (mNts.TxBuffer), mNts.DnsId, Server->Name);
      if (Length == 0) {
        NtsAdvance (TRUE);
        continue;
      }

      CopyMem (&mNts.QueryTo, &mNts.Dns[mNts.DnsIndex], sizeof (EFI_IPv4_ADDRESS));
      mNts.QueryPort = DNS_PORT;
    } else {
      mNts.Nonce = NtsRandom64 ();

      SntpBuildRequest (mNts.TxBuffer, mNts.Nonce);
      Length = SNTP_PACKET_SIZE;
      CopyMem (&mNts.QueryTo, &Server->Address, sizeof (EFI_IPv4_ADDRESS));
      mNts.QueryPort = SNTP_PORT;
    }

    Status = NtsTransmit (&mNts.QueryTo, mNts.QueryPort, Length);
    DEBUG ((
      DEBUG_VERBOSE,
      "NetworkTimeSync: %a query for %a to %d.%d.%d.%d: %r\n",
      (mNts.QueryPort == DNS_PORT) ? "DNS" : "NTP",
      Server->Name,
      mNts.QueryTo.Addr[0], mNts.QueryTo.Addr[1], mNts.QueryTo.Addr[2], mNts.QueryTo.Addr[3],
      Status
      ));
    if ((Status == EFI_NO_MAPPING) || (Status == EFI_NOT_READY) || (Status == EFI_NOT_STARTED)) {
      mNts.Problem = "no address yet";
      return;                     // address not usable yet: next tick
    }

    if (EFI_ERROR (Status)) {
      NtsAdvance (FALSE);
      continue;
    }

    mNts.Sent++;
    mNts.QuerySentNs      = NtsNowNs ();
    mNts.QueryOutstanding = TRUE;
    mNts.Problem          = "no server answered";
    return;
  }

  NtsFinish ("no server answered");
}

// ---------------------------------------------------------------------------
// Address
// ---------------------------------------------------------------------------

STATIC
VOID
NtsStopDhcp (
  VOID
  )
{
  if (mNts.Dhcp4 != NULL) {
    mNts.Dhcp4->Stop (mNts.Dhcp4);
    mNts.Dhcp4->Configure (mNts.Dhcp4, NULL);
    mNts.Dhcp4 = NULL;
  }

  if (mNts.Dhcp4Child != NULL) {
    NetLibDestroyServiceChild (mNts.Nic, mNts.ImageHandle, &gEfiDhcp4ServiceBindingProtocolGuid, mNts.Dhcp4Child);
    mNts.Dhcp4Child = NULL;
  }

  if (mNts.Dhcp4Event != NULL) {
    gBS->CloseEvent (mNts.Dhcp4Event);
    mNts.Dhcp4Event = NULL;
  }
}

STATIC
VOID
EFIAPI
NtsOnDhcpDone (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mNts.Dhcp4Completed = TRUE;
}

/**
  DHCP of our own, for a NIC whose IP4 policy is static with no address.
  Short retry intervals: the lease has to fit NTS_ADDRESS_BUDGET_MS.
**/
STATIC
EFI_STATUS
NtsStartDhcp (
  VOID
  )
{
  EFI_STATUS             Status;
  EFI_DHCP4_CONFIG_DATA  Config;
  STATIC UINT32          DiscoverTimeout[] = { 1, 2, 2 };
  STATIC UINT32          RequestTimeout[]  = { 1, 2 };
  STATIC UINT8           ParaListBuffer[2 + 4];
  EFI_DHCP4_PACKET_OPTION  *Options[1];

  Status = NetLibCreateServiceChild (
             mNts.Nic,
             mNts.ImageHandle,
             &gEfiDhcp4ServiceBindingProtocolGuid,
             &mNts.Dhcp4Child
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (mNts.Dhcp4Child, &gEfiDhcp4ProtocolGuid, (VOID **)&mNts.Dhcp4);
  if (EFI_ERROR (Status)) {
    mNts.Dhcp4 = NULL;
    return Status;
  }

  ParaListBuffer[0] = DHCP4_TAG_PARA_LIST_;
  ParaListBuffer[1] = 4;
  ParaListBuffer[2] = DHCP4_TAG_NETMASK_;
  ParaListBuffer[3] = DHCP4_TAG_ROUTER_;
  ParaListBuffer[4] = DHCP4_TAG_DNS_;
  ParaListBuffer[5] = DHCP4_TAG_NTP_;
  Options[0]        = (EFI_DHCP4_PACKET_OPTION *)ParaListBuffer;

  ZeroMem (&Config, sizeof (Config));
  Config.DiscoverTryCount = ARRAY_SIZE (DiscoverTimeout);
  Config.DiscoverTimeout  = DiscoverTimeout;
  Config.RequestTryCount  = ARRAY_SIZE (RequestTimeout);
  Config.RequestTimeout   = RequestTimeout;
  Config.OptionCount      = 1;
  Config.OptionList       = Options;

  Status = mNts.Dhcp4->Configure (mNts.Dhcp4, &Config);
  if (EFI_ERROR (Status)) {
    return Status;                // EFI_ACCESS_DENIED: another child owns DHCP
  }

  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, NtsOnDhcpDone, NULL, &mNts.Dhcp4Event);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mNts.Dhcp4Completed = FALSE;
  Status              = mNts.Dhcp4->Start (mNts.Dhcp4, mNts.Dhcp4Event);
  if (EFI_ERROR (Status) && (Status != EFI_ALREADY_STARTED)) {
    return Status;
  }

  mNts.State = NtsDhcp;
  return EFI_SUCCESS;
}

STATIC
VOID
NtsPollDhcp (
  VOID
  )
{
  EFI_STATUS           Status;
  EFI_DHCP4_MODE_DATA  Mode;

  if (!mNts.Dhcp4Completed) {
    return;
  }

  Status = mNts.Dhcp4->GetModeData (mNts.Dhcp4, &Mode);
  if (EFI_ERROR (Status) || (Mode.State != Dhcp4Bound)) {
    NtsFinish ("DHCP failed");
    return;
  }

  mNts.UseDefaultAddress = FALSE;
  CopyMem (&mNts.Station, &Mode.ClientAddress, sizeof (EFI_IPv4_ADDRESS));
  CopyMem (&mNts.Netmask, &Mode.SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  CopyMem (&mNts.Gateway, &Mode.RouterAddress, sizeof (EFI_IPv4_ADDRESS));
  NtsHarvestDhcpOptions (mNts.Dhcp4, Mode.ReplyPacket);
  DEBUG ((
    DEBUG_INFO,
    "NetworkTimeSync: own DHCP lease %d.%d.%d.%d via %d.%d.%d.%d, %u DNS, %u NTP server(s), %lu ms\n",
    mNts.Station.Addr[0], mNts.Station.Addr[1], mNts.Station.Addr[2], mNts.Station.Addr[3],
    mNts.Gateway.Addr[0], mNts.Gateway.Addr[1], mNts.Gateway.Addr[2], mNts.Gateway.Addr[3],
    (UINT32)mNts.DnsCount, (UINT32)mNts.ServerCount, NtsElapsedMs ()
    ));

  //
  // The lease data is copied; give the DHCP service back at once, so that a
  // PXE boot attempt in the meantime is not refused.
  //
  NtsStopDhcp ();

  NtsBuildServerList ();
  Status = NtsStartUdp ();
  if (EFI_ERROR (Status)) {
    NtsFinish ("UDP unavailable");
  }
}

/**
  Wait for the NIC's address, or start DHCP of our own when nothing will
  ever give it one.
**/
STATIC
VOID
NtsPollAddress (
  VOID
  )
{
  EFI_STATUS                      Status;
  EFI_IP4_CONFIG2_POLICY          Policy;
  UINTN                           Size;
  EFI_IP4_CONFIG2_INTERFACE_INFO  *Info;
  EFI_IPv4_ADDRESS                *Dns;
  UINTN                           Index;

  Size   = sizeof (Policy);
  Status = mNts.Ip4Config2->GetData (mNts.Ip4Config2, Ip4Config2DataTypePolicy, &Size, &Policy);
  if (EFI_ERROR (Status)) {
    return;
  }

  Info   = NULL;
  Size   = 0;
  Status = mNts.Ip4Config2->GetData (mNts.Ip4Config2, Ip4Config2DataTypeInterfaceInfo, &Size, NULL);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    Info = AllocateZeroPool (Size);
    if (Info == NULL) {
      return;
    }

    Status = mNts.Ip4Config2->GetData (mNts.Ip4Config2, Ip4Config2DataTypeInterfaceInfo, &Size, Info);
  }

  if (!EFI_ERROR (Status) && (Info != NULL) && (EFI_IP4 (Info->StationAddress) != 0)) {
    //
    // IP4 has an address (static, or a DHCP lease): use its default
    // address and routes as they are.
    //
    mNts.UseDefaultAddress = TRUE;
    CopyMem (&mNts.Station, &Info->StationAddress, sizeof (EFI_IPv4_ADDRESS));
    FreePool (Info);

    Size   = 0;
    Status = mNts.Ip4Config2->GetData (mNts.Ip4Config2, Ip4Config2DataTypeDnsServer, &Size, NULL);
    if ((Status == EFI_BUFFER_TOO_SMALL) && (Size >= sizeof (EFI_IPv4_ADDRESS))) {
      Dns = AllocateZeroPool (Size);
      if (Dns != NULL) {
        Status = mNts.Ip4Config2->GetData (mNts.Ip4Config2, Ip4Config2DataTypeDnsServer, &Size, Dns);
        if (!EFI_ERROR (Status)) {
          for (Index = 0; Index < Size / sizeof (EFI_IPv4_ADDRESS); Index++) {
            NtsAddDns (Dns[Index].Addr);
          }
        }

        FreePool (Dns);
      }
    }

    if (Policy == Ip4Config2PolicyDhcp) {
      NtsHarvestForeignLease ();
    }

    DEBUG ((
      DEBUG_INFO,
      "NetworkTimeSync: IP4 address %d.%d.%d.%d (%a policy), %u DNS, %u NTP server(s), %lu ms\n",
      mNts.Station.Addr[0], mNts.Station.Addr[1], mNts.Station.Addr[2], mNts.Station.Addr[3],
      (Policy == Ip4Config2PolicyDhcp) ? "DHCP" : "static",
      (UINT32)mNts.DnsCount, (UINT32)mNts.ServerCount, NtsElapsedMs ()
      ));

    NtsBuildServerList ();
    Status = NtsStartUdp ();
    if (EFI_ERROR (Status)) {
      NtsFinish ("UDP unavailable");
    }

    return;
  }

  if (Info != NULL) {
    FreePool (Info);
  }

  if (Policy == Ip4Config2PolicyDhcp) {
    mNts.Problem = "no DHCP lease";
    return;                       // IP4 is getting one; wait for it
  }

  //
  // Static policy and no address: nobody configured this NIC.  Do not set
  // the policy to DHCP (that would be stored), run a DHCP4 child instead.
  //
  Status = NtsStartDhcp ();
  if (EFI_ERROR (Status)) {
    NtsStopDhcp ();
    NtsFinish ("DHCP unavailable");
  }
}

/**
  Pick the first NIC with an IP4 stack and a link.
**/
STATIC
VOID
NtsPollNic (
  VOID
  )
{
  EFI_STATUS                   Status;
  EFI_HANDLE                   *Handles;
  UINTN                        Count;
  UINTN                        Index;
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_IP4_CONFIG2_PROTOCOL     *Ip4Config2;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiUdp4ServiceBindingProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    //
    // The stack binds within milliseconds of a connect.  Still nothing a
    // second later means it is not there (IPv4 or the whole network stack
    // disabled in setup); do not keep the timer running for the budget.
    //
    mNts.Problem = "no IPv4 stack";
    if (NtsElapsedMs () >= 1000) {
      NtsFinish (mNts.Problem);
    }

    return;
  }

  mNts.Problem = "no link";
  for (Index = 0; Index < Count; Index++) {
    Snp = NULL;
    Status = gBS->HandleProtocol (Handles[Index], &gEfiIp4Config2ProtocolGuid, (VOID **)&Ip4Config2);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = gBS->HandleProtocol (Handles[Index], &gEfiSimpleNetworkProtocolGuid, (VOID **)&Snp);
    if (!EFI_ERROR (Status) && Snp->Mode->MediaPresentSupported && !Snp->Mode->MediaPresent) {
      continue;
    }

    mNts.Nic        = Handles[Index];
    mNts.Ip4Config2 = Ip4Config2;
    mNts.State      = NtsWaitAddress;
    mNts.Problem    = "no address";
    mNts.LinkMs     = (INT64)NtsElapsedMs ();
    DEBUG ((
      DEBUG_INFO,
      "NetworkTimeSync: NIC %a after %lu ms, waiting for an IPv4 address\n",
      ((Snp != NULL) && Snp->Mode->MediaPresentSupported) ? "has link" : "picked (link not reported)",
      (UINT64)mNts.LinkMs
      ));
    break;
  }

  FreePool (Handles);
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

STATIC
VOID
NtsCleanup (
  VOID
  )
{
  NtsStopUdp ();
  NtsStopDhcp ();
  NtsDisconnectNics ();
  gBS->SetTimer (mNts.Timer, TimerCancel, 0);
  mNts.State = NtsDone;
}

/**
  Stop.  Problem NULL means the clock was set; otherwise it says why not.
  The protocol children are torn down from the next tick, never from inside
  one of their own completion callbacks.
**/
STATIC
VOID
NtsFinish (
  IN CONST CHAR8  *Problem
  )
{
  EFI_TIME     Now;
  CONST CHAR8  *Stage;

  if (mNts.State >= NtsFinishing) {
    return;
  }

  //
  // One line, whatever the outcome, naming the stage it stopped at.
  //
  if (Problem == NULL) {
    Stage = "clock set";
  } else {
    switch (mNts.State) {
      case NtsIdle:
      case NtsWaitNic:
        Stage = (AsciiStrCmp (Problem, "no IPv4 stack") == 0) ? "no IPv4 stack" : "no link";
        break;

      case NtsWaitAddress:
      case NtsDhcp:
        Stage = (AsciiStrCmp (Problem, "UDP unavailable") == 0) ? Problem : "no lease";
        break;

      case NtsQuery:
        if (mNts.Sent == 0) {
          Stage = "address, but nothing could be sent";
        } else if (mNts.TxDone == 0) {
          Stage = "nothing left the NIC (next hop never answered ARP: unicast receive?)";
        } else if (mNts.QueryPort == DNS_PORT) {
          Stage = "DNS timeout";
        } else {
          Stage = "NTP timeout";
        }

        break;

      default:
        Stage = Problem;
        break;
    }
  }

  DEBUG ((
    DEBUG_INFO,
    "NetworkTimeSync: stage: %a; link at %ld ms, address at %ld ms, %u server(s), %u DNS, round %u, "
    "%u sent, %u out, %u failed (last %r), %u in, %u DNS answer(s), %u NTP reply(s)\n",
    Stage,
    mNts.LinkMs,
    mNts.AddressMs,
    (UINT32)mNts.ServerCount,
    (UINT32)mNts.DnsCount,
    mNts.Round + 1,
    mNts.Sent,
    mNts.TxDone,
    mNts.TxFailed,
    mNts.LastTxStatus,
    mNts.Datagrams,
    mNts.DnsAnswers,
    mNts.NtpReplies
    ));

  if (Problem != NULL) {
    if (EFI_ERROR (gRT->GetTime (&Now, NULL))) {
      ZeroMem (&Now, sizeof (Now));
    }

    DEBUG ((
      DEBUG_INFO,
      "NetworkTimeSync: gave up after %lu ms (%a); clock stays at %04u-%02u-%02u %02u:%02u:%02u\n",
      NtsElapsedMs (), Problem,
      Now.Year, Now.Month, Now.Day, Now.Hour, Now.Minute, Now.Second
      ));
  }

  mNts.State = NtsFinishing;
}

STATIC
VOID
EFIAPI
NtsOnTick (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mNts.State == NtsDone) {
    return;
  }

  if (mNts.State == NtsFinishing) {
    NtsCleanup ();
    return;
  }

  if ((mNts.State < NtsQuery) && (NtsElapsedMs () >= NTS_ADDRESS_BUDGET_MS)) {
    NtsFinish (mNts.Problem);
    return;
  }

  if ((mNts.State == NtsQuery) && ((NtsNowNs () - mNts.QueryStartNs) / NS_PER_MS >= NTS_QUERY_BUDGET_MS)) {
    NtsFinish (mNts.Problem);
    return;
  }

  switch (mNts.State) {
    case NtsWaitNic:
      NtsPollNic ();
      break;

    case NtsWaitAddress:
      NtsPollAddress ();
      break;

    case NtsDhcp:
      NtsPollDhcp ();
      break;

    case NtsQuery:
      if (mNts.QueryOutstanding &&
          ((NtsNowNs () - mNts.QuerySentNs) / NS_PER_MS >= NTS_QUERY_TIMEOUT_MS))
      {
        if (mNts.TxPending) {
          mNts.Udp4->Cancel (mNts.Udp4, &mNts.TxToken);   // e.g. ARP never answered
        }

        DEBUG ((DEBUG_VERBOSE, "NetworkTimeSync: no answer from %d.%d.%d.%d\n",
          mNts.QueryTo.Addr[0], mNts.QueryTo.Addr[1], mNts.QueryTo.Addr[2], mNts.QueryTo.Addr[3]));
        NtsAdvance (FALSE);
      }

      if (!mNts.QueryOutstanding && !mNts.TxPending && (mNts.State == NtsQuery)) {
        NtsSendNext ();
      }

      break;

    default:
      break;
  }
}

/**
  The boot manager has connected what its discovery policy asks for.
**/
STATIC
VOID
EFIAPI
NtsOnDevicesConnected (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *SnpHandles;
  UINTN       SnpCount;
  EFI_HANDLE  *UdpHandles;
  UINTN       UdpCount;

  gBS->CloseEvent (Event);
  if (mNts.State != NtsIdle) {
    return;
  }

  if (PcdGet8 (PcdNetworkTimeSync) == 0) {
    DEBUG ((DEBUG_INFO, "NetworkTimeSync: disabled in setup\n"));
    mNts.State = NtsDone;
    return;
  }

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleNetworkProtocolGuid, NULL, &SnpCount, &SnpHandles);
  if (EFI_ERROR (Status) || (SnpCount == 0)) {
    DEBUG ((DEBUG_INFO, "NetworkTimeSync: no network interface\n"));
    mNts.State = NtsDone;
    return;
  }

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiUdp4ServiceBindingProtocolGuid, NULL, &UdpCount, &UdpHandles);
  if (EFI_ERROR (Status) || (UdpCount == 0)) {
    NtsConnectNics (SnpHandles, SnpCount);
  } else {
    FreePool (UdpHandles);
  }

  FreePool (SnpHandles);

  mNts.StartNs   = NtsNowNs ();
  mNts.Problem   = "no IPv4 stack";
  mNts.LinkMs    = -1;
  mNts.AddressMs = -1;
  mNts.State     = NtsWaitNic;
  Status       = gBS->SetTimer (mNts.Timer, TimerPeriodic, EFI_TIMER_PERIOD_MILLISECONDS (NTS_TICK_MS));
  if (EFI_ERROR (Status)) {
    NtsCleanup ();
  }
}

/**
  The OS is taking over.  No protocol calls and no memory changes are
  allowed here: stop the timer and let everything else go.
**/
STATIC
VOID
EFIAPI
NtsOnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mNts.Timer != NULL) {
    gBS->SetTimer (mNts.Timer, TimerCancel, 0);
  }

  mNts.State = NtsDone;
}

EFI_STATUS
EFIAPI
NetworkTimeSyncEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Event;

  ZeroMem (&mNts, sizeof (mNts));
  mNts.ImageHandle = ImageHandle;
  mNts.State       = NtsIdle;

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, NtsOnTick, NULL, &mNts.Timer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  NtsOnDevicesConnected,
                  NULL,
                  &gRockchipEventPlatformBmDevicesConnectedGuid,
                  &Event
                  );
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (mNts.Timer);
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  NtsOnExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &Event
                  );
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}

#!/usr/bin/env python3
"""
A passive Windows kernel-debugger peer, just enough of KDCOM to keep the target
moving and to read what it says.

The target retransmits every packet until a debugger acknowledges it, which is
why an unattended serial line shows the same line over and over and the boot
never advances.  This ACKs, so the boot proceeds and the bugcheck arrives.

Not a debugger: it never sends a manipulate request, so the target is free to
carry on.
"""
import os, sys, struct, time, termios

DEV = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
RAW = sys.argv[2] if len(sys.argv) > 2 else "kd-raw.bin"

PACKET_LEADER         = b"0000"          # 0x30303030, data
CONTROL_PACKET_LEADER = b"iiii"          # 0x69696969, control
TRAILER               = 0xAA

TYPE = {1: "STATE_CHANGE32", 2: "STATE_MANIPULATE", 3: "DEBUG_IO",
        4: "ACKNOWLEDGE", 5: "RESEND", 6: "RESET", 7: "STATE_CHANGE64",
        8: "POLL_BREAKIN", 9: "TRACE_IO", 10: "CONTROL_REQUEST", 11: "FILE_IO"}

DbgKdPrintStringApi = 0x00003230

fd = os.open(DEV, os.O_RDWR | os.O_NOCTTY)
raw = open(RAW, "ab", buffering=0)
buf = bytearray()
seen = {}          # PacketId -> times seen, to mark retransmissions

INITIAL_PACKET_ID = 0x80800000
SYNC_PACKET_ID    = 0x00000800

def send_ctrl(ptype, pid):
    pkt = CONTROL_PACKET_LEADER + struct.pack("<HHII", ptype, 0, pid, 0)
    n = os.write(fd, pkt)
    return n

DbgKdContinueApi2 = 0x0000313C
DBG_CONTINUE      = 0x00010002
MANIPULATE_SIZE   = 56          # sizeof(DBGKD_MANIPULATE_STATE64); not verified
                                # for ARM64 -- if continue is ignored, this is
                                # the first thing to doubt.
out_id = INITIAL_PACKET_ID

last_sent = None          # for RESEND; see the control handler

def send_data(ptype, payload):
    global out_id, last_sent
    csum = sum(payload) & 0xFFFFFFFF
    hdr = PACKET_LEADER + struct.pack("<HHII", ptype, len(payload), out_id, csum)
    last_sent = hdr + payload + bytes([TRAILER])
    os.write(fd, last_sent)
    out_id ^= 1

def send_continue(processor):
    pl = bytearray(MANIPULATE_SIZE)
    struct.pack_into("<IHHi", pl, 0, DbgKdContinueApi2, 0, processor, 0)
    struct.pack_into("<I", pl, 12, DBG_CONTINUE)
    send_data(2, bytes(pl))

STATUS_NOT_IMPLEMENTED = 0xC0000002

def send_fileio_fail(api):
    # winload asks the debugger to serve boot files when boot debugging is on.
    # Answering with a failure makes it fall back to its own file access, which
    # is what we want: we are here to watch, not to host its filesystem.
    pl = bytearray(64)
    struct.pack_into("<II", pl, 0, api, STATUS_NOT_IMPLEMENTED)
    send_data(11, bytes(pl))

def show(s):
    print(s, flush=True)

show(f"# listening on {DEV}, raw copy -> {RAW}")
last = time.time()
empty = 0
while True:
    try:
        chunk = os.read(fd, 4096)
    except BlockingIOError:
        time.sleep(0.01); continue
    if chunk:
        raw.write(chunk)
        buf += chunk
        last = time.time()
        empty = 0
    else:
        # A read of zero bytes on a cdc-acm node that has been unplugged never
        # turns into an error, so this used to spin at 100% CPU holding a
        # deleted /dev/ttyACM1 while the probe came back as ttyACM0 -- and the
        # wrapper could not move on, because it waits for this process to
        # exit.  Exit instead and let the wrapper find the new node.
        empty += 1
        if empty > 200:
            if not os.path.exists(DEV):
                sys.stderr.write(f"# {DEV} went away, exiting so the wrapper can reattach\n")
                sys.exit(0)
            empty = 0
        time.sleep(0.01)

    while True:
        # find the next leader of either kind
        i_d = buf.find(PACKET_LEADER)
        i_c = buf.find(CONTROL_PACKET_LEADER)
        cands = [i for i in (i_d, i_c) if i >= 0]
        if not cands:
            if len(buf) > 65536:
                del buf[:-16]
            break
        i = min(cands)
        if i:
            # bytes before a packet are the firmware's own console output
            pre = bytes(buf[:i])
            txt = pre.decode("utf-8", "replace").strip()
            if txt:
                show("  [console] " + txt.replace("\n", "\n  [console] "))
            del buf[:i]
        if len(buf) < 16:
            break
        leader = bytes(buf[:4])
        ptype, bcount, pid, csum = struct.unpack("<HHII", buf[4:16])
        need = 16 + bcount + (1 if leader == PACKET_LEADER else 0)

        # Firmware console text contains "0000", so a leader alone means
        # nothing.  Take the packet only if the checksum and the trailer agree;
        # otherwise step over one byte and look again.  Without this the parser
        # invents packets with four-digit lengths and, worse, answers them.
        if ptype not in TYPE or bcount > 4096:
            del buf[:1]
            continue
        if len(buf) < need:
            break
        payload = bytes(buf[16:16 + bcount])
        if (sum(payload) & 0xFFFFFFFF) != csum:
            del buf[:1]
            continue
        if leader == PACKET_LEADER and buf[16 + bcount] != TRAILER:
            del buf[:1]
            continue
        del buf[:need]

        name = TYPE.get(ptype, f"type{ptype}")
        if leader == PACKET_LEADER:
            n = seen.get(pid, 0) + 1
            seen[pid] = n
            if pid & SYNC_PACKET_ID:
                # The target is asking to synchronise, not to be acknowledged.
                # KDCOM answers a sync packet with a reset; an ACK is ignored
                # and it just keeps retransmitting.
                w = send_ctrl(6, 0)
                if n == 1 or n % 10 == 0:
                    show(f"-> RESET (sync packet id=0x{pid:08x}, seen {n}x, wrote {w}B)")
            else:
                send_ctrl(4, pid)
            if ptype == 3 and len(payload) >= 16:
                api, plevel, proc, slen = struct.unpack("<IHHI", payload[:12])
                if api == DbgKdPrintStringApi:
                    # Take the rest of the packet rather than trusting slen.
                    # DBGKD_DEBUG_IO's string length sits at a different offset
                    # than assumed here and the text came out clipped -- "BD:
                    # Boot Debugger Initiali" for "...Initialized".  The packet
                    # length already bounds the payload, so there is nothing to
                    # gain from the field and a truncated message can hide the
                    # part that matters.
                    s = payload[12:].decode("utf-8", "replace").rstrip("\x00").rstrip()
                    if n == 1:
                        show(f"KD: {s}")
                    continue
            if ptype == 11 and len(payload) >= 8:
                api = struct.unpack("<I", payload[:4])[0]
                path = payload[64:].decode("utf-16-le", "replace").strip("\x00")
                if n == 1:
                    show(f"KD[FILE_IO] api=0x{api:08x} {path!r} -> refusing")
                send_fileio_fail(api)
                continue
            if n == 1:
                show(f"KD[{name}] id=0x{pid:08x} len={bcount}")
            if ptype == 7 and len(payload) >= 8:
                api = struct.unpack("<I", payload[:4])[0]
                proc = struct.unpack("<H", payload[6:8])[0]
                kind = {0x3030: "EXCEPTION", 0x3031: "LOAD_SYMBOLS",
                        0x3032: "COMMAND_STRING"}.get(api & 0xFFFF, hex(api))
                if n == 1:
                    show(f"     state={kind} proc={proc}")
                    if kind == "EXCEPTION":
                        show("     " + payload[:160].hex(" "))
                send_continue(proc)
                if n == 1:
                    show(f"  -> CONTINUE (proc={proc})")
        else:
            if ptype == 5 and last_sent is not None:
                # RESEND: the target did not get our last packet and will not
                # move until it does. Without this the boot stops right after
                # the first CONTINUE -- seen on CM5-IO 2026-09-19, where the
                # exchange ended on `KD<ctrl RESEND>` and Windows never drew a
                # frame.
                os.write(fd, last_sent)
                show(f"KD<ctrl {name}> id=0x{pid:08x} -> resent {len(last_sent)}B")
            elif ptype == 6:
                # RESET: the target is resynchronising -- it does this when the
                # boot debugger hands over to the kernel's.  KDCOM answers a
                # RESET with a RESET and restarts packet numbering; log it and
                # do nothing and the kernel-side debugger never comes up, which
                # is what happened on CM5-IO 2026-09-20: winload's debugger
                # talked, then two bare RESETs and silence.
                out_id = INITIAL_PACKET_ID
                seen.clear()
                w = send_ctrl(6, 0)
                show(f"KD<ctrl RESET> id=0x{pid:08x} -> RESET ({w}B), ids restarted")
            elif ptype not in (4,):
                show(f"KD<ctrl {name}> id=0x{pid:08x}")

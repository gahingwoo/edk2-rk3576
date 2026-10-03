#!/usr/bin/env python3
"""
A passive Windows kernel-debugger peer, just enough of KDCOM to keep the target
moving and to read what it says.

The target retransmits every packet until a debugger acknowledges it, which is
why an unattended serial line shows the same line over and over and the boot
never advances.  This ACKs, so the boot proceeds and the bugcheck arrives.

Not a debugger: apart from continue, it sends manipulate requests only after a
bugcheck, to save the register context and stack (see dump_next).
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
seen = {}          # (PacketId, checksum) -> times seen, to mark retransmissions

INITIAL_PACKET_ID = 0x80800000
SYNC_PACKET_ID    = 0x00000800

def send_ctrl(ptype, pid):
    pkt = CONTROL_PACKET_LEADER + struct.pack("<HHII", ptype, 0, pid, 0)
    n = os.write(fd, pkt)
    return n

DbgKdContinueApi2 = 0x0000313C
DBG_CONTINUE      = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
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

def send_continue(processor, status=DBG_CONTINUE):
    pl = bytearray(MANIPULATE_SIZE)
    struct.pack_into("<IHHi", pl, 0, DbgKdContinueApi2, 0, processor, 0)
    struct.pack_into("<I", pl, 12, status)
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

# After a bugcheck the target sits in the debugger, and a bugcheck's own
# breakpoint carries no clue to what faulted: on 2026-10-03 a
# WHEA_INTERNAL_ERROR (0x122, 9, 0x11 = SEA) came back as `brk` in
# KeBugCheckEx and nothing else. So on the first exception after a "Fatal
# System Error" print, ask for the register context and read the stack before
# continuing; the trap frame of the abort and the return addresses into the
# driver that took it are in there. Saved raw to kd-dump-<time>.bin; the module
# lines above carry each image's base and size to resolve them against.
DbgKdReadVirtualMemoryApi = 0x00003130
DbgKdGetContextApi        = 0x00003132
DbgKdGetContextExApi      = 0x0000315F
ARM64_CONTEXT_SIZE        = 0x390
STACK_CHUNK               = 0x400
STACK_CHUNKS              = 48

dump = None        # {"proc", "stage", "sp", "next", "out"} while a dump is in progress
bugchecked = False

def send_manip(api, proc, fill=None):
    pl = bytearray(MANIPULATE_SIZE)
    struct.pack_into("<IHH", pl, 0, api, 0, proc)
    if fill:
        fill(pl)
    send_data(2, bytes(pl))

def ask_context(proc, ex=False):
    if ex:
        send_manip(DbgKdGetContextExApi, proc,
                   lambda pl: struct.pack_into("<III", pl, 16, 0, ARM64_CONTEXT_SIZE, 0))
    else:
        send_manip(DbgKdGetContextApi, proc)

def ask_read(proc, va, n):
    send_manip(DbgKdReadVirtualMemoryApi, proc,
               lambda pl: struct.pack_into("<QII", pl, 16, va, n, 0))

def dump_next():
    """Ask for the next stack chunk, or finish and let the target go."""
    global dump
    d = dump
    if d["next"] < STACK_CHUNKS:
        ask_read(d["proc"], d["sp"] + d["next"] * STACK_CHUNK, STACK_CHUNK)
        d["next"] += 1
        return
    d["out"].close()
    show(f"  [dump] done: {STACK_CHUNKS * STACK_CHUNK:#x} bytes of stack from "
         f"sp=0x{d['sp']:016x} -> {d['path']}")
    proc = d["proc"]
    dump = None
    send_continue(proc)

def on_manip(payload):
    """A STATE_MANIPULATE reply from the target, while a dump is running."""
    global dump
    if dump is None or len(payload) < MANIPULATE_SIZE:
        return
    api, _, proc, st = struct.unpack("<IHHI", payload[:12])
    data = payload[MANIPULATE_SIZE:]
    d = dump
    if api in (DbgKdGetContextApi, DbgKdGetContextExApi):
        if st != 0 or len(data) < 0x110:
            show(f"  [dump] get-context api=0x{api:x} status=0x{st:08x} len={len(data)}")
            if api == DbgKdGetContextApi:
                ask_context(proc, ex=True)
                return
            dump = None
            send_continue(proc)
            return
        x = struct.unpack_from("<29Q", data, 8)
        fp, lr, sp, pc = struct.unpack_from("<4Q", data, 0xF0)
        show(f"  [dump] context proc={proc} pc=0x{pc:016x} lr=0x{lr:016x} "
             f"sp=0x{sp:016x} fp=0x{fp:016x}")
        show("  [dump] " + " ".join(f"x{i}={v:x}" for i, v in enumerate(x)))
        d["sp"] = sp & ~0xF
        d["out"].write(struct.pack("<4sQ", b"CTX0", len(data)) + data)
        dump_next()
    elif api == DbgKdReadVirtualMemoryApi:
        got = struct.unpack_from("<I", payload, 28)[0]
        va = struct.unpack_from("<Q", payload, 16)[0]
        if st != 0:
            show(f"  [dump] read 0x{va:016x} status=0x{st:08x}")
        d["out"].write(struct.pack("<4sQI", b"MEM0", va, len(data)) + data)
        dump_next()

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
            # A retransmission repeats the id *and* the contents. Keying on the
            # id alone hid almost everything: KDCOM alternates data packet ids
            # between ...000 and ...001, so from the third packet on every new
            # one looked like a resend and was answered but never printed --
            # 2026-10-01, a 67-module boot logged as three.
            key = (pid, csum)
            n = seen.get(key, 0) + 1
            seen[key] = n
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
                        if "Fatal System Error" in s:
                            bugchecked = True
                    continue
            if ptype == 2:
                if n == 1:
                    on_manip(payload)
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
                    if kind == "LOAD_SYMBOLS" and len(payload) >= 0x24:
                        # DBGKD_LOAD_SYMBOLS64.PathNameLength sits at 0x20, and
                        # the path itself is appended after the fixed-size
                        # state-change record, i.e. it is the packet's tail.
                        # This is the list of every driver the kernel has
                        # loaded, which is what a boot that dies with
                        # INACCESSIBLE_BOOT_DEVICE needs answered first.
                        plen = struct.unpack("<I", payload[0x20:0x24])[0]
                        if 0 < plen <= len(payload):
                            mod = payload[-plen:].split(b"\0")[0].decode("ascii", "replace")
                            base = struct.unpack("<Q", payload[40:48])[0]
                            size = struct.unpack("<I", payload[60:64])[0]
                            show(f"     module: {mod} base=0x{base:016x} size=0x{size:x}")
                # DBGKD_WAIT_STATE_CHANGE64: ProgramCounter at 24, then the
                # EXCEPTION_RECORD64 (ExceptionCode at 32).
                status = DBG_CONTINUE
                if kind == "EXCEPTION" and len(payload) >= 40:
                    pc = struct.unpack("<Q", payload[24:32])[0]
                    code = struct.unpack("<I", payload[32:36])[0]
                    if pc < 0x8000000000000000:
                        # A user-mode exception (a process's own breakpoint,
                        # say). DBG_CONTINUE re-runs a brk forever: on
                        # 2026-10-02 svchost -k utcsvc's brk #0xf000 looped on
                        # all eight CPUs and flooded the line. Hand it back
                        # to the process instead.
                        status = DBG_EXCEPTION_NOT_HANDLED
                    if n == 1:
                        show(f"     exception 0x{code:08x} at pc 0x{pc:016x}"
                             f" -> {'NOT_HANDLED' if status != DBG_CONTINUE else 'CONTINUE'}")
                if kind == "EXCEPTION" and bugchecked and dump is None and n == 1:
                    bugchecked = False
                    path = time.strftime("kd-dump-%Y%m%d-%H%M%S.bin")
                    dump = {"proc": proc, "sp": 0, "next": 0, "path": path,
                            "out": open(path, "wb")}
                    dump["out"].write(struct.pack("<4sI", b"EXC0", len(payload)) + payload)
                    show(f"  [dump] bugcheck: reading context and stack of proc {proc}")
                    ask_context(proc)
                    continue
                if dump is not None:
                    continue   # the target is waiting on our reads; do not continue it twice
                send_continue(proc, status)
                if n == 1:
                    show(f"  -> CONTINUE (proc={proc}, status=0x{status:08x})")
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

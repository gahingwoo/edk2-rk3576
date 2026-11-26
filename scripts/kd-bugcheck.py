#!/usr/bin/env python3
"""
kd-bugcheck.py: a minimal Windows KDCOM peer that names the module behind a
bugcheck, for boards where WinDbg is not available and no crash dump is
written.

What it does, as the only reader of the debug UART:

  * keeps the boot moving (ACK, RESET, RESEND, continue, FILE_IO refusal, an
    answer to DbgPrompt), like kd-listen.py;
  * prints DbgPrint output ("KD: ...");
  * keeps a table of loaded images from LOAD_SYMBOLS state changes and prints
    "load <name> <base> <size>" for each;
  * on the breakpoint KeBugCheckEx takes after "*** Fatal System Error" (or on
    any kernel exception with --dump-exceptions), reads the register context,
    the stack, the debugger data block (KiBugCheckData: code and 4 params) and
    the loaded module list, then prints every stack value that falls inside a
    loaded image as module+offset, marks the ones that follow a BL/BLR, walks
    the frame-pointer chain and names the first non-kernel return address as
    the suspect;
  * writes a timestamped log, the received bytes (.bin) and the sent bytes
    (-tx.bin);
  * --offline FILE.bin replays a saved capture through the same parser and the
    same dump logic (answers come from the replies recorded in the capture),
    so a run can be re-analysed on the host, and older kd-listen.py captures
    work too.

Not a debugger: it never writes target memory or registers.

Structure layouts relied on (64-bit, little endian).  Sources:
  [R1] ReactOS sdk/include/reactos/windbgkd.h (mirror of the WDK windbgkd.h)
  [R2] ReactOS sdk/include/psdk/wdbgexts.h (KDDEBUGGER_DATA64,
       DBGKD_GET_VERSION64)
  [R3] Wine include/winnt.h, struct _ARM64_NT_CONTEXT (same layout as the
       WDK's ARM64 CONTEXT)
  [R4] ReactOS sdk/include/ndk/ldrtypes.h, LDR_DATA_TABLE_ENTRY (64-bit)
  [C]  verified against the 2026-10-03 CM5-IO captures
       (~/Desktop/woa-kd-captures/kd-dump-20261003-*.bin, Windows 11 ARM64)

  KD_PACKET [R1]: ULONG Leader ("0000" data / "iiii" control), USHORT Type,
    USHORT ByteCount, ULONG PacketId, ULONG Checksum (byte sum of payload);
    data packets end with 0xAA.  PACKET_MAX_SIZE 4000.
  DBGKD_ANY_WAIT_STATE_CHANGE (STATE_CHANGE64) [R1][C]:
    0x00 NewState  0x04 ProcessorLevel  0x06 Processor  0x08 NumberProcessors
    0x10 Thread    0x18 ProgramCounter  0x20 union u
    0xC0 control report   (whole packet 0xF0 = 240 bytes [C])
  DBGKM_EXCEPTION64 at 0x20 [R1][C]: EXCEPTION_RECORD64 {0x20 ExceptionCode,
    0x24 Flags, 0x28 Record, 0x30 ExceptionAddress, 0x38 NumberParameters,
    0x40 ExceptionInformation[15]}, 0xB8 FirstChance
  ARM64 control report at 0xC0 [C]: ULONG Cpsr, ULONG InstructionCount,
    UCHAR InstructionStream[16].  NOTE: [R1] declares ARM64_DBGKD_CONTROL_REPORT
    as {Bvr, Wvr, InstructionCount, Stream}; that does not match what
    Windows sends.  In the captures 0xC0 equals CONTEXT.Cpsr, 0xC4 = 16 and
    0xC8 decodes to "brk #0xf000; ret" at the reported PC, i.e. the ARM32
    ARM_DBGKD_CONTROL_REPORT layout.
  DBGKD_LOAD_SYMBOLS64 at 0x20 [R1][C]: 0x20 PathNameLength, 0x28 BaseOfDll,
    0x30 ProcessId, 0x38 CheckSum, 0x3C SizeOfImage, 0x40 UnloadSymbols; the
    path follows the 240-byte record [C].
  DBGKD_MANIPULATE_STATE64 [R1]: 0x00 ApiNumber, 0x04 ProcessorLevel,
    0x06 Processor, 0x08 ReturnStatus, 0x10 union u (8-aligned), 56 bytes;
    data (memory, context) follows the 56 bytes.
    u.ReadMemory: 0x10 TargetBaseAddress, 0x18 TransferCount,
      0x1C ActualBytesRead
    u.ContextEx: 0x10 Offset, 0x14 ByteCount, 0x18 BytesCopied
    u.Continue2: 0x10 ContinueStatus, then the control set (all zero here)
    u.GetVersion64 [R2]: 0x10 Major, 0x12 Minor, 0x14 ProtocolVersion,
      0x15 KdSecondaryVersion, 0x16 Flags, 0x18 MachineType, 0x20 KernBase,
      0x28 PsLoadedModuleList, 0x30 DebuggerDataList
  DBGKD_DEBUG_IO [R1]: 0x00 ApiNumber, 0x04 ProcessorLevel, 0x06 Processor,
    0x08 LengthOfString / LengthOfPromptString, 0x0C LengthOfStringRead,
    text at 0x10.
  DBGKD_FILE_IO [R1]: 0x00 ApiNumber, 0x04 Status, 0x08 union (56 bytes),
    64 bytes, path (UTF-16) after it.
  KDDEBUGGER_DATA64 [R2]: 0x00 LIST_ENTRY64, 0x10 OwnerTag 'KDBG',
    0x14 Size, 0x18 KernBase, 0x48 PsLoadedModuleList, 0x88 KiBugcheckData.
  KiBugCheckData: ULONG_PTR[5] = code, param1..param4.
  ARM64 CONTEXT [R3][C]: 0x000 ContextFlags, 0x004 Cpsr, 0x008 X0..X28,
    0x0F0 Fp, 0x0F8 Lr, 0x100 Sp, 0x108 Pc, 0x110 V[32], 0x310 Fpcr,
    0x314 Fpsr, 0x318 Bcr[8], 0x338 Bvr[8], 0x378 Wcr[2], 0x380 Wvr[2];
    size 0x390.
  KLDR_DATA_TABLE_ENTRY (64-bit) [R4]: 0x00 InLoadOrderLinks, 0x30 DllBase,
    0x40 SizeOfImage, 0x48 FullDllName, 0x58 BaseDllName
    (UNICODE_STRING64: USHORT Length, USHORT MaximumLength, 0x08 Buffer).
"""
import argparse
import glob
import os
import re
import struct
import sys
import time

# --------------------------------------------------------------------------
# Protocol constants [R1]

DATA_LEADER = b"0000"
CTRL_LEADER = b"iiii"
TRAILER = 0xAA
PACKET_MAX_SIZE = 4000
INITIAL_PACKET_ID = 0x80800000
SYNC_PACKET_ID = 0x00000800

PT_STATE_CHANGE32 = 1
PT_MANIPULATE = 2
PT_DEBUG_IO = 3
PT_ACK = 4
PT_RESEND = 5
PT_RESET = 6
PT_STATE_CHANGE64 = 7
PT_POLL_BREAKIN = 8
PT_TRACE_IO = 9
PT_CONTROL_REQUEST = 10
PT_FILE_IO = 11
DATA_TYPES = {PT_STATE_CHANGE32, PT_MANIPULATE, PT_DEBUG_IO, PT_STATE_CHANGE64,
              PT_TRACE_IO, PT_CONTROL_REQUEST, PT_FILE_IO}
CTRL_TYPES = {PT_ACK, PT_RESEND, PT_RESET}
TYPE_NAME = {1: "STATE_CHANGE32", 2: "STATE_MANIPULATE", 3: "DEBUG_IO",
             4: "ACKNOWLEDGE", 5: "RESEND", 6: "RESET", 7: "STATE_CHANGE64",
             8: "POLL_BREAKIN", 9: "TRACE_IO", 10: "CONTROL_REQUEST",
             11: "FILE_IO"}

DbgKdExceptionStateChange = 0x3030
DbgKdLoadSymbolsStateChange = 0x3031
DbgKdCommandStringStateChange = 0x3032

DbgKdReadVirtualMemoryApi = 0x3130
DbgKdGetContextApi = 0x3132
DbgKdContinueApi2 = 0x313C
DbgKdGetVersionApi = 0x3146
DbgKdGetContextExApi = 0x315F

DbgKdPrintStringApi = 0x3230
DbgKdGetStringApi = 0x3231

DbgKdCreateFileApi = 0x3430

DBG_CONTINUE = 0x00010002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
STATUS_NOT_IMPLEMENTED = 0xC0000002
STATUS_BREAKPOINT = 0x80000003

MANIPULATE_SIZE = 56
DEBUG_IO_SIZE = 16
FILE_IO_SIZE = 64
STATE_CHANGE_SIZE = 240
CONTROL_REPORT_OFF = 0xC0
ARM64_CONTEXT_SIZE = 0x390
MACHINE_ARM64 = 0xAA64

KDBG_TAG = 0x4742444B          # 'KDBG'
KDBG_KIBUGCHECKDATA = 0x88
KDBG_PSLOADEDMODULELIST = 0x48

KERNEL_MIN = 0x8000000000000000

DBG_STATUS = {1: "CONTROL_C", 2: "SYSRQ", 3: "BUGCHECK_FIRST",
              4: "BUGCHECK_SECOND", 5: "FATAL", 6: "DEBUG_CONTROL",
              7: "WORKER"}

BUGCHECK_NAME = {
    0x0A: "IRQL_NOT_LESS_OR_EQUAL", 0x19: "BAD_POOL_HEADER",
    0x1E: "KMODE_EXCEPTION_NOT_HANDLED", 0x3B: "SYSTEM_SERVICE_EXCEPTION",
    0x50: "PAGE_FAULT_IN_NONPAGED_AREA", 0x7B: "INACCESSIBLE_BOOT_DEVICE",
    0x7E: "SYSTEM_THREAD_EXCEPTION_NOT_HANDLED", 0x9F: "DRIVER_POWER_STATE_FAILURE",
    0xA5: "ACPI_BIOS_ERROR", 0x116: "VIDEO_TDR_FAILURE",
    0x122: "WHEA_INTERNAL_ERROR", 0x124: "WHEA_UNCORRECTABLE_ERROR",
    0x133: "DPC_WATCHDOG_VIOLATION", 0x139: "KERNEL_SECURITY_CHECK_FAILURE",
    0x13A: "KERNEL_MODE_HEAP_CORRUPTION", 0xC2: "BAD_POOL_CALLER",
    0xC4: "DRIVER_VERIFIER_DETECTED_VIOLATION",
    0xC5: "DRIVER_CORRUPTED_EXPOOL", 0xD1: "DRIVER_IRQL_NOT_LESS_OR_EQUAL",
    0xEF: "CRITICAL_PROCESS_DIED", 0x1A: "MEMORY_MANAGEMENT",
    0x1D3: "DRIVER_PNP_WATCHDOG",
}
# BAD_POOL_CALLER parameter 1 (Microsoft "Bug Check 0xC2" page).  Only the
# subtypes whose meaning is certain; look the rest up on that page.
BAD_POOL_CALLER_P1 = {
    0x07: "pool freed twice (p4 = address of the block's pool header)",
    0x08: "pool allocated at a bad IRQL (p2 = IRQL, p3 = pool type, p4 = size)",
    0x09: "pool freed at a bad IRQL (p2 = IRQL, p3 = pool type, p4 = address)",
}

KERNEL_IMAGES = {"ntoskrnl.exe", "hal.dll", "kdcom.dll", "kd.dll"}


def checksum(b):
    return sum(b) & 0xFFFFFFFF


def frame_data(ptype, pid, payload):
    return (DATA_LEADER + struct.pack("<HHII", ptype, len(payload), pid,
                                      checksum(payload)) + payload + bytes([TRAILER]))


def frame_ctrl(ptype, pid=0):
    return CTRL_LEADER + struct.pack("<HHII", ptype, 0, pid, 0)


def short_name(path):
    return re.split(r"[\\/]", path)[-1]


# --------------------------------------------------------------------------
# Framing


class Packet:
    __slots__ = ("ctrl", "ptype", "pid", "payload", "csum")

    def __init__(self, ctrl, ptype, pid, payload, csum):
        self.ctrl, self.ptype, self.pid, self.payload, self.csum = ctrl, ptype, pid, payload, csum

    def __repr__(self):
        return (f"Packet({'ctrl' if self.ctrl else 'data'} "
                f"{TYPE_NAME.get(self.ptype, self.ptype)} id=0x{self.pid:08x} "
                f"len={len(self.payload)})")


class Framer:
    """Splits the received byte stream into checked packets and console text.

    Firmware console text contains "0000", so a leader alone means nothing: a
    data packet is taken only if its type is one the target sends, its length
    fits, the checksum matches and the 0xAA trailer is in place; a control
    packet only if its type is ACK/RESEND/RESET and its byte count is 0.
    Anything else is stepped over one byte at a time and counted as text.
    """

    def __init__(self):
        self.buf = bytearray()
        self.text = bytearray()

    def feed(self, data):
        """Returns Packets and console lines (str), in stream order."""
        self.buf += data
        out = []
        while True:
            i_d = self.buf.find(DATA_LEADER)
            i_c = self.buf.find(CTRL_LEADER)
            cands = [i for i in (i_d, i_c) if i >= 0]
            if not cands:
                keep = 3            # could be the start of a leader
                if len(self.buf) > keep:
                    self.text += self.buf[:-keep]
                    del self.buf[:-keep]
                break
            i = min(cands)
            if i:
                self.text += self.buf[:i]
                del self.buf[:i]
            if len(self.buf) < 16:
                break
            leader = bytes(self.buf[:4])
            ptype, bcount, pid, csum = struct.unpack_from("<HHII", self.buf, 4)
            if leader == CTRL_LEADER:
                if ptype in CTRL_TYPES and bcount == 0:
                    del self.buf[:16]
                    out.extend(self._lines())
                    out.append(Packet(True, ptype, pid, b"", csum))
                    continue
                self._reject()
                continue
            if ptype not in DATA_TYPES or bcount > PACKET_MAX_SIZE:
                self._reject()
                continue
            need = 16 + bcount + 1
            if len(self.buf) < need:
                break
            payload = bytes(self.buf[16:16 + bcount])
            if checksum(payload) != csum or self.buf[16 + bcount] != TRAILER:
                self._reject()
                continue
            del self.buf[:need]
            out.extend(self._lines())
            out.append(Packet(False, ptype, pid, payload, csum))
        out.extend(self._lines())
        return out

    def _reject(self):
        self.text += self.buf[:1]
        del self.buf[:1]

    def _lines(self, flush=False):
        res = []
        while True:
            j = self.text.find(b"\n")
            if j < 0:
                break
            res.append(self.text[:j])
            del self.text[:j + 1]
        if flush or len(self.text) > 4096:
            if self.text:
                res.append(bytes(self.text))
            self.text.clear()
        return [r.decode("utf-8", "replace").rstrip("\r") for r in res
                if r.strip(b"\r\x00 ")]

    def flush(self):
        self.text += self.buf
        self.buf.clear()
        return self._lines(flush=True)


# --------------------------------------------------------------------------
# Parsed structures


def parse_state_change(p):
    sc = {}
    sc["state"], sc["level"], sc["proc"], sc["nproc"] = struct.unpack_from("<IHHI", p, 0)
    sc["thread"], sc["pc"] = struct.unpack_from("<QQ", p, 0x10)
    return sc


def parse_exception(p):
    e = parse_state_change(p)
    (e["code"], e["flags"], e["record"], e["address"],
     e["nparams"]) = struct.unpack_from("<IIQQI", p, 0x20)
    e["params"] = list(struct.unpack_from("<15Q", p, 0x40))
    e["first_chance"] = struct.unpack_from("<I", p, 0xB8)[0]
    if len(p) >= CONTROL_REPORT_OFF + 24:
        cpsr, icount = struct.unpack_from("<II", p, CONTROL_REPORT_OFF)
        e["cpsr"] = cpsr
        e["icount"] = icount
        n = min(icount, 16)
        e["istream"] = p[CONTROL_REPORT_OFF + 8:CONTROL_REPORT_OFF + 8 + n]
    return e


def parse_load_symbols(p):
    m = parse_state_change(p)
    plen, = struct.unpack_from("<I", p, 0x20)
    m["base"], m["pid"] = struct.unpack_from("<QQ", p, 0x28)
    m["checksum"], m["size"] = struct.unpack_from("<II", p, 0x38)
    m["unload"] = p[0x40] != 0
    if len(p) >= STATE_CHANGE_SIZE + plen:
        raw = p[STATE_CHANGE_SIZE:STATE_CHANGE_SIZE + plen]
    else:
        raw = p[-plen:] if 0 < plen <= len(p) else b""
    m["name"] = raw.split(b"\0")[0].decode("ascii", "replace")
    return m


class Context:
    def __init__(self, data):
        self.raw = data
        self.flags, self.cpsr = struct.unpack_from("<II", data, 0)
        self.x = list(struct.unpack_from("<29Q", data, 0x08))
        self.fp, self.lr, self.sp, self.pc = struct.unpack_from("<4Q", data, 0xF0)


class Modules:
    """Loaded images: base -> (name, size)."""

    def __init__(self):
        self.by_base = {}

    def add(self, base, size, name):
        self.by_base[base] = (name, size)

    def remove(self, base):
        self.by_base.pop(base, None)

    def clear(self):
        self.by_base.clear()

    def __len__(self):
        return len(self.by_base)

    def lookup(self, va):
        for base, (name, size) in self.by_base.items():
            if base <= va < base + size:
                return name, va - base
        return None

    def sym(self, va):
        r = self.lookup(va)
        if not r:
            return f"0x{va:016x}"
        return f"{short_name(r[0])}+0x{r[1]:x}"

    def copy(self):
        m = Modules()
        m.by_base = dict(self.by_base)
        return m


def is_call_insn(insn):
    """True for BL, BLR, BLRAA/BLRAB/BLRAAZ/BLRABZ."""
    if (insn & 0xFC000000) == 0x94000000:          # BL imm26
        return True
    if (insn & 0xFFFFFC1F) == 0xD63F0000:          # BLR Xn
        return True
    if (insn & 0xFEFFF800) == 0xD63F0800:          # BLRAA/BLRAB(Z)
        return True
    return False


# --------------------------------------------------------------------------
# The dump: a generator that yields requests and receives replies.  Live mode
# sends each request over KD; offline mode answers it from the replies
# recorded in the capture.  So both run the same code and make the same
# decisions.
#
# Requests:  ("ctx", ex)  ("read", va, n)  ("version",)
# Replies:   dict with "status" and api-specific fields, or None (no reply).


def ok(r):
    return r is not None and r.get("status") == 0


def dump_flow(exc, modules, opts):
    rep = {"exc": exc, "modules": modules, "ctx": None, "stack_base": 0,
           "stack": b"", "version": None, "kdbg": None, "bugcheck": None,
           "insn": {}, "notes": []}

    r = yield ("ctx", False)
    if not (ok(r) and len(r["data"]) >= 0x110):
        rep["notes"].append(f"GetContext failed ({_st(r)}), trying GetContextEx")
        r = yield ("ctx", True)
    if ok(r) and len(r["data"]) >= 0x110:
        rep["ctx"] = Context(r["data"])
    else:
        rep["notes"].append(f"no register context ({_st(r)})")

    # Stack, from SP up, until the first failed or short read (the end of the
    # kernel stack; reading past it fails with 0xC0000001 [C]).
    if rep["ctx"]:
        sp = rep["ctx"].sp & ~0xF
        rep["stack_base"] = sp
        buf = bytearray()
        while len(buf) < opts.stack_bytes:
            n = min(opts.chunk, opts.stack_bytes - len(buf))
            r = yield ("read", sp + len(buf), n)
            if not r or not r["data"]:
                rep["notes"].append(f"stack read at 0x{sp + len(buf):016x} stopped ({_st(r)})")
                break
            buf += r["data"]
            if len(r["data"]) < n:
                break
        rep["stack"] = bytes(buf)

    # Version -> debugger data block -> KiBugCheckData, PsLoadedModuleList.
    r = yield ("version",)
    if ok(r):
        rep["version"] = r
        ddl = r["debugger_data_list"]
        kdbg = None
        if ddl:
            h = yield ("read", ddl, 16)
            if ok(h) and len(h["data"]) >= 8:
                flink = struct.unpack_from("<Q", h["data"], 0)[0]
                b = yield ("read", flink, 0x100)
                if ok(b) and len(b["data"]) >= 0x90:
                    tag, size = struct.unpack_from("<II", b["data"], 0x10)
                    if tag == KDBG_TAG:
                        kdbg = {"addr": flink, "size": size,
                                "kernbase": struct.unpack_from("<Q", b["data"], 0x18)[0],
                                "psloaded": struct.unpack_from("<Q", b["data"], KDBG_PSLOADEDMODULELIST)[0],
                                "kibugcheckdata": struct.unpack_from("<Q", b["data"], KDBG_KIBUGCHECKDATA)[0]}
                    else:
                        rep["notes"].append(
                            f"debugger data block at 0x{flink:016x} has tag 0x{tag:08x}, "
                            "not 'KDBG' (encoded?); bugcheck code comes from the "
                            "Fatal System Error print only")
                else:
                    rep["notes"].append(f"debugger data block read failed ({_st(b)})")
            else:
                rep["notes"].append(f"DebuggerDataList read failed ({_st(h)})")
        rep["kdbg"] = kdbg
        if kdbg and kdbg["kibugcheckdata"]:
            b = yield ("read", kdbg["kibugcheckdata"], 40)
            if ok(b) and len(b["data"]) >= 40:
                v = struct.unpack_from("<5Q", b["data"], 0)
                rep["bugcheck"] = (v[0] & 0xFFFFFFFF,) + tuple(v[1:])

        # Module list: always useful when the LOAD_SYMBOLS table is thin (the
        # listener was attached late); --walk-modules forces it.
        head = (kdbg or {}).get("psloaded") or r["ps_loaded_module_list"]
        need = opts.walk_modules or len(modules) < 5
        pc = exc.get("pc", 0)
        if not modules.lookup(pc):
            need = True
        if head and need and not opts.no_walk_modules:
            walked = yield from walk_modules(head, opts)
            for base, size, name in walked:
                if not modules.lookup(base):
                    modules.add(base, size, name)
            rep["notes"].append(f"walked PsLoadedModuleList: {len(walked)} entries")
    else:
        rep["notes"].append(f"GetVersion failed ({_st(r)})")

    # Which stack values are return addresses: read the instruction before
    # each and keep the ones that follow a call.
    cands = []
    for off, val in stack_values(rep):
        if val & 3 == 0 and modules.lookup(val) and val not in cands:
            cands.append(val)
    if rep["ctx"]:
        for v in (rep["ctx"].lr,):
            if modules.lookup(v) and v not in cands:
                cands.append(v)
    for va in cands[:opts.max_verify]:
        r = yield ("read", va - 4, 4)
        if ok(r) and len(r["data"]) == 4:
            rep["insn"][va] = struct.unpack("<I", r["data"])[0]
    return rep


def walk_modules(head, opts):
    out = []
    h = yield ("read", head, 16)
    if not (ok(h) and len(h["data"]) >= 8):
        return out
    entry = struct.unpack_from("<Q", h["data"], 0)[0]
    seen = set()
    while entry and entry != head and entry not in seen and len(out) < opts.max_modules:
        seen.add(entry)
        e = yield ("read", entry, 0x68)
        if not (ok(e) and len(e["data"]) >= 0x68):
            break
        d = e["data"]
        nxt = struct.unpack_from("<Q", d, 0)[0]
        base, = struct.unpack_from("<Q", d, 0x30)
        size, = struct.unpack_from("<I", d, 0x40)
        nlen, = struct.unpack_from("<H", d, 0x58)
        nbuf, = struct.unpack_from("<Q", d, 0x60)
        name = f"image@{base:x}"
        if nbuf and 0 < nlen <= 512:
            n = yield ("read", nbuf, nlen)
            if ok(n) and n["data"]:
                name = n["data"].decode("utf-16-le", "replace")
        if base and size:
            out.append((base, size, name))
        entry = nxt
    return out


def _st(r):
    if r is None:
        return "no reply"
    return f"status 0x{r.get('status', 0):08x}"


def stack_values(rep):
    s = rep["stack"]
    for off in range(0, len(s) - 7, 8):
        yield off, struct.unpack_from("<Q", s, off)[0]


def fp_chain(rep, limit=64):
    """ARM64 frame records: [fp] = caller fp, [fp+8] = return address."""
    ctx = rep["ctx"]
    if not ctx:
        return []
    s, base = rep["stack"], rep["stack_base"]
    out = []
    fp = ctx.fp
    for _ in range(limit):
        if not (base <= fp and fp + 16 <= base + len(s)) or fp & 7:
            break
        nfp, lr = struct.unpack_from("<QQ", s, fp - base)
        if not lr:
            break
        out.append((fp, lr))
        if nfp <= fp:
            break
        fp = nfp
    return out


def format_report(rep, fatal):
    L = []
    exc, mods, ctx = rep["exc"], rep["modules"], rep["ctx"]
    L.append("=" * 72)
    L.append(f"BUGCHECK / EXCEPTION REPORT  (modules known: {len(mods)})")
    bc = rep["bugcheck"]
    src = "KiBugCheckData"
    if not bc and fatal and fatal.get("code") is not None:
        bc = (fatal["code"],) + tuple(fatal.get("params", (0, 0, 0, 0)))
        src = "Fatal System Error print"
    if bc:
        name = BUGCHECK_NAME.get(bc[0], "")
        L.append(f"bugcheck 0x{bc[0]:08x} {name}  [{src}]")
        L.append("  params " + ", ".join(f"0x{p:016x}" for p in bc[1:5]))
        if bc[0] == 0xC2 and bc[1] in BAD_POOL_CALLER_P1:
            L.append(f"  p1=0x{bc[1]:x}: {BAD_POOL_CALLER_P1[bc[1]]}")
        for p in bc[1:5]:
            if mods.lookup(p):
                L.append(f"  param 0x{p:016x} = {mods.sym(p)}")
    else:
        L.append("bugcheck code: unknown (no KiBugCheckData, no Fatal System Error print)")
    if fatal and fatal.get("driver"):
        L.append(f"  kernel says: {fatal['driver']}")
    L.append(f"exception 0x{exc['code']:08x} at 0x{exc['address']:016x} = {mods.sym(exc['address'])}"
             f"  first_chance={exc['first_chance']}")
    L.append(f"  processor {exc['proc']} of {exc['nproc']}, thread 0x{exc['thread']:016x}, "
             f"pc 0x{exc['pc']:016x} = {mods.sym(exc['pc'])}")
    if exc["nparams"]:
        L.append("  info " + " ".join(f"0x{v:x}" for v in exc["params"][:min(exc['nparams'], 15)]))
    if "cpsr" in exc:
        L.append(f"  control report: cpsr 0x{exc['cpsr']:08x}, {exc['icount']} instruction bytes: "
                 f"{exc['istream'].hex(' ')}")
    v = rep["version"]
    if v:
        L.append(f"  kernel {v['major']}.{v['minor']} machine 0x{v['machine']:04x} "
                 f"KernBase 0x{v['kern_base']:016x} PsLoadedModuleList 0x{v['ps_loaded_module_list']:016x} "
                 f"DebuggerDataList 0x{v['debugger_data_list']:016x}")
    if rep["kdbg"]:
        k = rep["kdbg"]
        L.append(f"  KDBG at 0x{k['addr']:016x}, KiBugCheckData at 0x{k['kibugcheckdata']:016x}")
    if ctx:
        st = DBG_STATUS.get(ctx.x[0], "")
        L.append(f"context: pc 0x{ctx.pc:016x} = {mods.sym(ctx.pc)}")
        L.append(f"         lr 0x{ctx.lr:016x} = {mods.sym(ctx.lr)}")
        L.append(f"         sp 0x{ctx.sp:016x}  fp 0x{ctx.fp:016x}  cpsr 0x{ctx.cpsr:08x}")
        L.append(f"         x0 0x{ctx.x[0]:x}{' (DBG_STATUS_' + st + ')' if st and exc['code'] == STATUS_BREAKPOINT else ''}"
                 f"  x1 0x{ctx.x[1]:x}  x2 0x{ctx.x[2]:x}  x3 0x{ctx.x[3]:x}")
        L.append("         " + " ".join(f"x{i}={val:x}" for i, val in enumerate(ctx.x)))
    for n in rep["notes"]:
        L.append(f"note: {n}")

    chain = fp_chain(rep)
    if chain:
        L.append(f"frame-pointer chain ({len(chain)} frames):")
        L.append(f"  #00 pc {mods.sym(ctx.pc)}")
        for i, (fp, lr) in enumerate(chain, 1):
            L.append(f"  #{i:02d} fp 0x{fp:016x} ret 0x{lr:016x} = {mods.sym(lr)}")

    L.append(f"stack scan: {len(rep['stack'])} bytes from 0x{rep['stack_base']:016x}, "
             "values inside a loaded image\n"
             "  ('fp' = return slot of a frame record, 'call' = the instruction before it is BL/BLR):")
    verified = []
    fp_slots = {fp + 8 - rep["stack_base"] for fp, _ in chain}
    for off, val in stack_values(rep):
        if mods.lookup(val):
            insn = rep["insn"].get(val)
            mark = "fp" if off in fp_slots else ""
            if insn is not None and is_call_insn(insn):
                mark = (mark + " call").strip()
                verified.append((off, val))
            L.append(f"  sp+0x{off:04x}  0x{val:016x}  {mark:7}  {mods.sym(val)}")
    if verified:
        L.append("call chain from the stack (innermost first, return addresses after a BL/BLR):")
        last = None
        for off, val in verified:
            s = mods.sym(val)
            if s != last:
                L.append(f"  sp+0x{off:04x}  {s}")
            last = s
    sus = suspect(rep, verified, chain)
    if sus:
        L.append(f"SUSPECT: {sus}")
    else:
        L.append("SUSPECT: none outside the kernel found on the stack that was read")
    L.append("=" * 72)
    return L


def suspect(rep, verified, chain):
    """First return address outside the kernel images.  The frame-pointer
    chain is the most reliable source, then BL/BLR-verified stack values, then
    any code-looking stack value (which can also be a data pointer)."""
    mods = rep["modules"]
    sources = [("frame-pointer chain", [lr for _, lr in chain]),
               ("BL/BLR-verified stack values", [v for _, v in verified]),
               ("unverified stack values (may be data pointers)",
                [v for _, v in stack_values(rep) if v & 3 == 0 and mods.lookup(v)])]
    for what, seq in sources:
        for v in seq:
            hit = mods.lookup(v)
            if hit and short_name(hit[0]).lower() not in KERNEL_IMAGES:
                return f"{mods.sym(v)}  (first non-kernel return address, from the {what})"
    return None


# --------------------------------------------------------------------------
# The peer


class Opts:
    stack_bytes = 0x4000
    chunk = 0x400
    max_verify = 128
    max_modules = 512
    walk_modules = False
    no_walk_modules = False
    dump_exceptions = False
    dump_breaks = False
    max_dumps = 4
    prompt_answer = "i"
    reply_timeout = 5.0
    resend_after = 1.0


class Log:
    def __init__(self, path=None, quiet=False, stamp=True, clock=time.time):
        self.f = open(path, "a", buffering=1) if path else None
        self.quiet = quiet
        self.lines = []
        self.stamp = stamp
        self.clock = clock

    def __call__(self, s):
        for line in str(s).split("\n"):
            if self.stamp:
                t = self.clock()
                line = time.strftime("[%H:%M:%S", time.localtime(t)) + f".{int(t * 1000) % 1000:03d}] " + line
            self.lines.append(line)
            if not self.quiet:
                print(line, flush=True)
            if self.f:
                self.f.write(line + "\n")

    def close(self):
        if self.f:
            self.f.close()
            self.f = None


class Engine:
    """KD host side.  tx=None makes it passive (offline replay)."""

    def __init__(self, tx, log, opts=None, clock=time.monotonic):
        self.tx = tx
        self.log = log
        self.opts = opts or Opts()
        self.clock = clock
        self.framer = Framer()
        self.modules = Modules()
        self.out_id = INITIAL_PACKET_ID
        self.last_rx = None
        self.rx_repeat = 0
        self.last_sent = None
        self.sent_at = 0.0
        self.unacked = None
        self.resends = 0
        self.last_reset_sent = -1e9
        self.flow = None
        self.flow_proc = 0
        self.pending = None
        self.pending_at = 0.0
        self.armed = False
        self.fatal = None
        self.print_tail = ""
        self.dumps = 0
        self.reports = []
        self.continues = 0
        # offline only
        self.session = None

    # -- tx ----------------------------------------------------------------
    def _send(self, b):
        if self.tx:
            self.tx(b)

    def send_ctrl(self, ptype, pid=0):
        if ptype == PT_RESET:
            self.last_reset_sent = self.clock()
        self._send(frame_ctrl(ptype, pid))

    def send_data(self, ptype, payload):
        if not self.tx:
            return
        pkt = frame_data(ptype, self.out_id, payload)
        self.last_sent = pkt
        self.unacked = self.out_id
        self.sent_at = self.clock()
        self.resends = 0
        self._send(pkt)
        self.out_id ^= 1

    def manip(self, api, proc, fill=None, extra=b""):
        pl = bytearray(MANIPULATE_SIZE)
        struct.pack_into("<IHH", pl, 0, api, 0, proc)
        if fill:
            fill(pl)
        self.send_data(PT_MANIPULATE, bytes(pl) + extra)

    def send_continue(self, proc, status=DBG_CONTINUE):
        # u.Continue2.ContinueStatus is at 0x10: the union in
        # DBGKD_MANIPULATE_STATE64 is 8-aligned [R1].  (kd-listen.py writes it
        # at 0x0C, the padding, so the target always read 0 = success.)
        self.manip(DbgKdContinueApi2, proc,
                   lambda pl: struct.pack_into("<I", pl, 0x10, status))
        self.continues += 1

    # -- rx ----------------------------------------------------------------
    def feed(self, data):
        for item in self.framer.feed(data):
            if isinstance(item, str):
                self.log("  [console] " + item)
            elif item.ctrl:
                self.on_ctrl(item)
            else:
                self.on_data(item)

    def finish(self):
        for line in self.framer.flush():
            self.log("  [console] " + line)
        if self.session is not None:
            self._run_offline_session()

    def tick(self):
        """Call periodically in live mode: resend unACKed packets, time out
        replies."""
        now = self.clock()
        if self.unacked is not None and now - self.sent_at > self.opts.resend_after:
            if self.resends < 10:
                self.resends += 1
                self.sent_at = now
                self._send(self.last_sent)
                self.log(f"  (no ACK for id 0x{self.unacked:08x}, resent, try {self.resends})")
        if self.pending and now - self.pending_at > self.opts.reply_timeout:
            self.log(f"  [dump] no reply to {self.pending}, skipping it")
            self._advance(None)

    def on_ctrl(self, pkt):
        if pkt.ptype == PT_ACK:
            if self.unacked is not None and pkt.pid == self.unacked:
                self.unacked = None
        elif pkt.ptype == PT_RESEND:
            if self.last_sent is not None and self.tx:
                self._send(self.last_sent)
                self.log(f"KD<RESEND> -> resent {len(self.last_sent)} bytes")
        elif pkt.ptype == PT_RESET:
            # The target restarted its numbering.  Answer it, unless it is the
            # echo of a RESET we sent a moment ago (the target answers our
            # RESET with one [R1 kdcom]); answering the echo ping-pongs.
            self.out_id = INITIAL_PACKET_ID
            self.last_rx = None
            self.unacked = None
            if self.tx and self.clock() - self.last_reset_sent > 1.0:
                self.send_ctrl(PT_RESET)
                self.log("KD<RESET> -> RESET, ids restarted")
            else:
                self.log("KD<RESET> (echo of ours), ids restarted")

    def on_data(self, pkt):
        if pkt.pid & SYNC_PACKET_ID:
            # A sync packet wants a RESET; the target then resends the same
            # packet with a normal id, which is the one acted on.  Acting on
            # both would answer one state change twice.
            if self.tx:
                self.send_ctrl(PT_RESET)
            self.log(f"KD sync packet id=0x{pkt.pid:08x} -> RESET")
            return
        self.send_ctrl(PT_ACK, pkt.pid)
        # The target does not send a new packet until the last one is ACKed,
        # so a retransmission can only repeat the previous packet.
        key = (pkt.pid, pkt.csum, len(pkt.payload), pkt.ptype)
        if key == self.last_rx:
            self.rx_repeat += 1
            if self.rx_repeat % 10 == 0:
                self.log(f"  (id 0x{pkt.pid:08x} retransmitted {self.rx_repeat}x)")
            return
        self.last_rx = key
        self.rx_repeat = 0
        p = pkt.payload
        if pkt.ptype == PT_DEBUG_IO and len(p) >= DEBUG_IO_SIZE:
            self.on_debug_io(p)
        elif pkt.ptype == PT_STATE_CHANGE64 and len(p) >= 0x20:
            self.on_state_change(p)
        elif pkt.ptype == PT_MANIPULATE and len(p) >= MANIPULATE_SIZE:
            self.on_manip(p)
        elif pkt.ptype == PT_FILE_IO and len(p) >= 8:
            api = struct.unpack_from("<I", p, 0)[0]
            path = p[FILE_IO_SIZE:].decode("utf-16-le", "replace").strip("\x00")
            self.log(f"KD[FILE_IO] api=0x{api:x} {path!r} -> not implemented")
            pl = bytearray(FILE_IO_SIZE)
            struct.pack_into("<II", pl, 0, api, STATUS_NOT_IMPLEMENTED)
            self.send_data(PT_FILE_IO, bytes(pl))
        else:
            self.log(f"KD[{TYPE_NAME.get(pkt.ptype, pkt.ptype)}] id=0x{pkt.pid:08x} len={len(p)}")

    def on_debug_io(self, p):
        api, _, proc, l1, l2 = struct.unpack_from("<IHHII", p, 0)
        text = p[DEBUG_IO_SIZE:]
        if api == DbgKdPrintStringApi:
            s = text.decode("utf-8", "replace").rstrip("\x00")
            for line in s.rstrip().split("\n"):
                self.log("KD: " + line.rstrip("\r"))
            self._watch_fatal(s)
        elif api == DbgKdGetStringApi:
            prompt = text[:l1].decode("utf-8", "replace").rstrip("\x00")
            ans = self.opts.prompt_answer.encode()
            self.log(f"KD prompt: {prompt!r} -> answering {self.opts.prompt_answer!r}")
            hdr = bytearray(DEBUG_IO_SIZE)
            struct.pack_into("<IHHII", hdr, 0, DbgKdGetStringApi, 0, proc, 0, len(ans))
            self.send_data(PT_DEBUG_IO, bytes(hdr) + ans)
        else:
            self.log(f"KD[DEBUG_IO] api=0x{api:x}")

    FATAL_RE = re.compile(r"Fatal System Error:\s*0x([0-9a-fA-F]+)\s*\(\s*0x([0-9a-fA-F]+)\s*,"
                          r"\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*,\s*0x([0-9a-fA-F]+)\s*\)")

    def _watch_fatal(self, s):
        self.print_tail = (self.print_tail + s)[-2048:]
        if "Fatal System Error" in s:
            self.armed = True
            self.fatal = {"code": None, "params": (0, 0, 0, 0), "driver": None}
        if self.fatal is not None:
            m = self.FATAL_RE.search(self.print_tail)
            if m and self.fatal["code"] is None:
                self.fatal["code"] = int(m.group(1), 16)
                self.fatal["params"] = tuple(int(m.group(i), 16) for i in range(2, 6))
            d = re.search(r"Driver at fault:\s*\n?\s*\*+\s*([^\s]+)", self.print_tail)
            if d:
                self.fatal["driver"] = "Driver at fault: " + d.group(1)

    def on_state_change(self, p):
        if self.session is not None:
            self._run_offline_session()
        state, = struct.unpack_from("<I", p, 0)
        proc, = struct.unpack_from("<H", p, 6)
        kind = state & 0xFFFF
        if kind == DbgKdLoadSymbolsStateChange and len(p) >= 0x44:
            m = parse_load_symbols(p)
            if m["unload"]:
                self.modules.remove(m["base"])
                self.log(f"unload {m['name']} 0x{m['base']:016x}")
            else:
                if short_name(m["name"]).lower() == "ntoskrnl.exe":
                    self.modules.clear()    # a new boot: old bases are stale
                    self.dumps = 0
                    self.armed = False
                self.modules.add(m["base"], m["size"], m["name"])
                self.log(f"load {m['name']} 0x{m['base']:016x} 0x{m['size']:x}")
            self.send_continue(proc)
            return
        if kind == DbgKdExceptionStateChange and len(p) >= 0xC0:
            e = parse_exception(p)
            user = e["pc"] < KERNEL_MIN
            self.log(f"EXCEPTION 0x{e['code']:08x} at 0x{e['address']:016x} = "
                     f"{self.modules.sym(e['address'])} proc {proc} thread 0x{e['thread']:016x} "
                     f"pc 0x{e['pc']:016x} first_chance={e['first_chance']}"
                     f"{' (user mode)' if user else ''}")
            if "cpsr" in e:
                self.log(f"  control report: cpsr 0x{e['cpsr']:08x} insns {e['istream'].hex(' ')}")
            want = False
            if not user and self.dumps < self.opts.max_dumps:
                if self.armed:
                    want = True
                elif self.opts.dump_breaks and e["code"] == STATUS_BREAKPOINT:
                    want = True
                elif self.opts.dump_exceptions and e["code"] != STATUS_BREAKPOINT:
                    want = True
            if want:
                self.armed = False
                self.dumps += 1
                self.log(f"  [dump] reading context, stack, KiBugCheckData of proc {proc}")
                self.flow = dump_flow(e, self.modules.copy(), self.opts)
                self.flow_proc = proc
                if self.tx:
                    self._advance(None, first=True)
                else:
                    self.session = []
                return
            self.send_continue(proc, DBG_EXCEPTION_NOT_HANDLED if user else DBG_CONTINUE)
            return
        name = {DbgKdCommandStringStateChange: "COMMAND_STRING"}.get(kind, f"0x{state:x}")
        self.log(f"KD state change {name} proc {proc}")
        self.send_continue(proc)

    # -- manipulate replies ---------------------------------------------
    @staticmethod
    def parse_reply(p):
        api, _, proc, st = struct.unpack_from("<IHHI", p, 0)
        r = {"api": api, "proc": proc, "status": st, "data": p[MANIPULATE_SIZE:]}
        if api == DbgKdReadVirtualMemoryApi:
            r["va"], r["count"], r["got"] = struct.unpack_from("<QII", p, 0x10)
            r["data"] = r["data"][:r["got"]] if r["got"] <= len(r["data"]) else r["data"]
        elif api == DbgKdGetContextExApi:
            r["offset"], r["count"], r["got"] = struct.unpack_from("<III", p, 0x10)
        elif api == DbgKdGetVersionApi:
            (r["major"], r["minor"], r["proto"], r["secondary"], r["vflags"],
             r["machine"]) = struct.unpack_from("<HHBBHH", p, 0x10)
            r["kern_base"], r["ps_loaded_module_list"], r["debugger_data_list"] = \
                struct.unpack_from("<QQQ", p, 0x20)
        return r

    def on_manip(self, p):
        r = self.parse_reply(p)
        if self.session is not None:
            self.session.append(r)
            return
        if self.pending is None:
            self.log(f"KD[MANIPULATE] api=0x{r['api']:x} status=0x{r['status']:08x} (unexpected)")
            return
        want = self._api_of(self.pending)
        if r["api"] != want:
            self.log(f"  [dump] reply api=0x{r['api']:x} while waiting for 0x{want:x}, ignored")
            return
        self._advance(r)

    @staticmethod
    def _api_of(req):
        if req[0] == "ctx":
            return DbgKdGetContextExApi if req[1] else DbgKdGetContextApi
        if req[0] == "read":
            return DbgKdReadVirtualMemoryApi
        return DbgKdGetVersionApi

    def _send_request(self, req):
        proc = self.flow_proc
        if req[0] == "ctx":
            if req[1]:
                self.manip(DbgKdGetContextExApi, proc,
                           lambda pl: struct.pack_into("<III", pl, 0x10, 0, ARM64_CONTEXT_SIZE, 0))
            else:
                self.manip(DbgKdGetContextApi, proc)
        elif req[0] == "read":
            va, n = req[1], req[2]
            self.manip(DbgKdReadVirtualMemoryApi, proc,
                       lambda pl: struct.pack_into("<QII", pl, 0x10, va, n, 0))
        else:
            self.manip(DbgKdGetVersionApi, proc)

    def _advance(self, reply, first=False):
        try:
            req = next(self.flow) if first else self.flow.send(reply)
        except StopIteration as stop:
            self._report(stop.value)
            self.flow = None
            self.pending = None
            self.send_continue(self.flow_proc)
            self.log(f"  [dump] done, continued proc {self.flow_proc}")
            return
        self.pending = req
        self.pending_at = self.clock()
        self._send_request(req)

    def _report(self, rep):
        self.reports.append(rep)
        for line in format_report(rep, self.fatal):
            self.log(line)
        self.fatal = None

    def _run_offline_session(self):
        """Replay the dump flow against the replies recorded in a capture."""
        recorded, self.session = self.session, None
        cursor = 0
        flow = self.flow
        self.flow = None
        reply = None
        try:
            req = next(flow)
            while True:
                want = self._api_of(req)
                reply = None
                for j in range(cursor, len(recorded)):
                    r = recorded[j]
                    if r["api"] != want:
                        continue
                    if want == DbgKdReadVirtualMemoryApi and r.get("va") != req[1]:
                        continue
                    reply = r
                    cursor = j + 1
                    break
                req = flow.send(reply)
        except StopIteration as stop:
            self._report(stop.value)


# --------------------------------------------------------------------------
# Serial I/O


PROBE_VID, PROBE_PID = "2e8a", "000c"     # Raspberry Pi Debug Probe


def find_probe():
    for t in sorted(glob.glob("/sys/class/tty/ttyACM*")):
        d = os.path.realpath(os.path.join(t, "device"))
        while d not in ("/", ""):
            v = os.path.join(d, "idVendor")
            if os.path.exists(v):
                try:
                    if (open(v).read().strip() == PROBE_VID and
                            open(os.path.join(d, "idProduct")).read().strip() == PROBE_PID):
                        return "/dev/" + os.path.basename(t)
                except OSError:
                    pass
                break
            d = os.path.dirname(d)
    return None


def other_holders(dev):
    real = os.path.realpath(dev)
    me = str(os.getpid())
    res = []
    for fd in glob.glob("/proc/[0-9]*/fd/*"):
        pid = fd.split("/")[2]
        if pid == me:
            continue
        try:
            if os.path.realpath(fd) == real:
                cmd = open(f"/proc/{pid}/cmdline", "rb").read().replace(b"\0", b" ").decode()
                res.append((pid, cmd.strip()))
        except OSError:
            pass
    return sorted(set(res))


class Port:
    """pyserial if present, else termios."""

    def __init__(self, dev, baud):
        self.dev = dev
        try:
            import serial  # noqa: F401
            self.s = serial.Serial(dev, baud, timeout=0.02, exclusive=True)
            self.fd = None
        except ImportError:
            import termios
            self.s = None
            self.fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            a = termios.tcgetattr(self.fd)
            speed = getattr(termios, f"B{baud}")
            a[0] = 0
            a[1] = 0
            a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            a[3] = 0
            a[4] = a[5] = speed
            a[6][termios.VMIN] = 0
            a[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, a)

    def read(self):
        if self.s:
            n = self.s.in_waiting
            return self.s.read(n or 1)
        try:
            b = os.read(self.fd, 65536)
        except BlockingIOError:
            b = b""
        if not b:
            if not os.path.exists(self.dev):
                raise OSError("device went away")
            time.sleep(0.01)
        return b

    def write(self, b):
        if self.s:
            self.s.write(b)
            self.s.flush()
        else:
            os.write(self.fd, b)

    def close(self):
        try:
            if self.s:
                self.s.close()
            else:
                os.close(self.fd)
        except Exception:
            pass


def run_live(args, opts):
    stamp = time.strftime("%Y%m%d-%H%M%S")
    base = os.path.join(args.out_dir, f"kd-bugcheck-{stamp}")
    log = Log(base + ".log")
    raw = open(base + ".bin", "ab", buffering=0)
    txf = open(base + "-tx.bin", "ab", buffering=0)
    log(f"# kd-bugcheck: log {base}.log, rx {base}.bin, tx {base}-tx.bin")

    port = None

    def tx(b):
        txf.write(b)
        if port:
            port.write(b)

    eng = Engine(tx, log, opts)
    try:
        while True:
            dev = args.device if args.device != "auto" else find_probe()
            if not dev or not os.path.exists(dev):
                time.sleep(0.3)
                continue
            holders = other_holders(dev)
            if holders and not args.force:
                for pid, cmd in holders:
                    log(f"REFUSING: {dev} is also open in PID {pid}: {cmd}")
                log("Stop the other reader (UEFI serial logger, kd-listen, screen, minicom) "
                    "or pass --force.")
                return 2
            try:
                port = Port(dev, args.baud)
            except Exception as ex:                       # noqa: BLE001
                log(f"# cannot open {dev}: {ex}")
                time.sleep(0.5)
                continue
            log(f"# attached {dev} at {args.baud}")
            try:
                while True:
                    b = port.read()
                    if b:
                        raw.write(b)
                        eng.feed(b)
                    eng.tick()
            except Exception as ex:                       # noqa: BLE001
                log(f"# detached {dev}: {ex}")
                port.close()
                port = None
                time.sleep(0.3)
    except KeyboardInterrupt:
        log("# stopped")
    finally:
        eng.finish()
        log(f"# {len(eng.modules)} modules known, {len(eng.reports)} report(s); log {base}.log")
        log.close()
        raw.close()
        txf.close()
    return 0


def run_offline(args, opts):
    out = args.log or (os.path.splitext(args.offline)[0] + "-offline.log")
    log = Log(out, stamp=False)
    log(f"# offline replay of {args.offline} -> {out}")
    eng = Engine(None, log, opts)
    with open(args.offline, "rb") as f:
        while True:
            b = f.read(1 << 16)
            if not b:
                break
            eng.feed(b)
    eng.finish()
    log(f"# {len(eng.modules)} modules known at the end, {len(eng.reports)} report(s)")
    log.close()
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("device", nargs="?", default="/dev/ttyACM0",
                    help="serial device, or 'auto' for the Raspberry Pi Debug Probe (default /dev/ttyACM0)")
    ap.add_argument("--baud", type=int, default=1500000)
    ap.add_argument("--out-dir", default=".", help="where the .log/.bin files go")
    ap.add_argument("--offline", metavar="FILE.bin", help="replay a saved capture instead of a live port")
    ap.add_argument("--log", help="offline: log file (default FILE-offline.log)")
    ap.add_argument("--stack-bytes", type=lambda s: int(s, 0), default=Opts.stack_bytes)
    ap.add_argument("--chunk", type=lambda s: int(s, 0), default=Opts.chunk,
                    help="bytes per memory read (max 0xF00)")
    ap.add_argument("--dump-exceptions", action="store_true",
                    help="also dump on any kernel exception that is not a breakpoint")
    ap.add_argument("--dump-breaks", action="store_true",
                    help="also dump on every kernel breakpoint, not only after a Fatal System Error")
    ap.add_argument("--max-dumps", type=int, default=Opts.max_dumps)
    ap.add_argument("--walk-modules", action="store_true",
                    help="always walk PsLoadedModuleList at a dump (default: only if the table is thin)")
    ap.add_argument("--no-walk-modules", action="store_true")
    ap.add_argument("--prompt-answer", default=Opts.prompt_answer,
                    help="answer to DbgPrompt, e.g. an assertion's (b)reak/(i)gnore (default i)")
    ap.add_argument("--force", action="store_true", help="start even if another process holds the port")
    args = ap.parse_args(argv)

    opts = Opts()
    opts.stack_bytes = args.stack_bytes
    opts.chunk = min(args.chunk, 0xF00)
    opts.dump_exceptions = args.dump_exceptions
    opts.dump_breaks = args.dump_breaks
    opts.max_dumps = args.max_dumps
    opts.walk_modules = args.walk_modules
    opts.no_walk_modules = args.no_walk_modules
    opts.prompt_answer = args.prompt_answer
    if args.offline:
        return run_offline(args, opts)
    return run_live(args, opts)


if __name__ == "__main__":
    sys.exit(main())

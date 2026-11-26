#!/usr/bin/env python3
"""
Host-side tests for kd-bugcheck.py: packet framing and checksums, the
sync/ACK/RESET rules, and a full bugcheck exchange against a fake ARM64
target that answers GetContext, ReadVirtualMemory and GetVersion from a
synthetic memory image.  The bytes the fake target sent are then replayed
through --offline mode, which must reach the same verdict.

    python3 scripts/test_kd_bugcheck.py
"""
import importlib.util
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("kdb", os.path.join(HERE, "kd-bugcheck.py"))
kdb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(kdb)

K = 0xFFFFF80000000000          # ntoskrnl.exe
KSIZE = 0x1000000
D = 0xFFFFF80010000000          # mydrv.sys, the culprit
DSIZE = 0x10000
X = 0xFFFFF80020000000          # extra.sys, only in PsLoadedModuleList
S = 0xFFFF900000001000          # stack, 0x2000 bytes mapped
SSIZE = 0x2000

PC = K + 0x2088B0               # brk #0xf000 in DbgBreakPointWithStatus
LR = K + 0x4926F0


def q(*v):
    return struct.pack(f"<{len(v)}Q", *v)


class FakeTarget:
    """Just enough of a Windows ARM64 KD target."""

    def __init__(self, kdbg_tag=kdb.KDBG_TAG):
        self.mem = {}
        self.pid = kdb.INITIAL_PACKET_ID
        self.sent = bytearray()          # what the target put on the wire
        self.host_framer = kdb.Framer()
        self.host_packets = []
        self.continues = []
        self.engine = None
        self.build_memory(kdbg_tag)

    # memory ------------------------------------------------------------
    def map(self, va, data):
        self.mem[va] = bytes(data)

    def read(self, va, n):
        for base, data in self.mem.items():
            if base <= va < base + len(data):
                return data[va - base:va - base + n]
        return b""

    def build_memory(self, kdbg_tag):
        st = bytearray(SSIZE)
        # frame records: [fp] = caller fp, [fp+8] = return address
        struct.pack_into("<QQ", st, 0x000, S + 0x40, K + 0x253430)
        struct.pack_into("<Q", st, 0x020, D + 0x8000)          # a data pointer
        struct.pack_into("<QQ", st, 0x040, S + 0x100, D + 0x1234)
        struct.pack_into("<QQ", st, 0x100, S + 0x200, K + 0x300000)
        struct.pack_into("<QQ", st, 0x200, 0, 0)
        self.map(S, st)
        self.map(K + 0x25342C, struct.pack("<I", 0xD63F0100))   # blr x8
        self.map(D + 0x1230, struct.pack("<I", 0x94000010))     # bl
        self.map(K + 0x2FFFFC, struct.pack("<I", 0x94000020))   # bl
        self.map(LR - 4, struct.pack("<I", 0x97FFFF00))         # bl
        # GetVersion -> DebuggerDataList -> KDDEBUGGER_DATA64
        self.map(K + 0x600000, q(K + 0x610000, K + 0x610000))
        kd = bytearray(0x100)
        struct.pack_into("<QQII", kd, 0, K + 0x600000, K + 0x600000, kdbg_tag, 0x368)
        struct.pack_into("<Q", kd, 0x18, K)
        struct.pack_into("<Q", kd, kdb.KDBG_PSLOADEDMODULELIST, K + 0x500000)
        struct.pack_into("<Q", kd, kdb.KDBG_KIBUGCHECKDATA, K + 0x620000)
        self.map(K + 0x610000, kd)
        self.map(K + 0x620000, q(0xC2, 0x7, 0x1234, 0x5678, 0xFFFF800012345000))
        # PsLoadedModuleList: head -> E1 (ntoskrnl) -> E2 (mydrv) -> E3 (extra) -> head
        entries = [(K + 0x510000, K, KSIZE, "ntoskrnl.exe"),
                   (K + 0x520000, D, DSIZE, "mydrv.sys"),
                   (K + 0x530000, X, 0x4000, "extra.sys")]
        self.map(K + 0x500000, q(entries[0][0], entries[-1][0]))
        for i, (addr, base, size, name) in enumerate(entries):
            nxt = entries[i + 1][0] if i + 1 < len(entries) else K + 0x500000
            e = bytearray(0x68)
            struct.pack_into("<Q", e, 0, nxt)
            struct.pack_into("<Q", e, 0x30, base)
            struct.pack_into("<I", e, 0x40, size)
            u = name.encode("utf-16-le")
            struct.pack_into("<HHIQ", e, 0x58, len(u), len(u) + 2, 0, addr + 0x100)
            self.map(addr, e)
            self.map(addr + 0x100, u)

    def context(self):
        c = bytearray(kdb.ARM64_CONTEXT_SIZE)
        struct.pack_into("<II", c, 0, 0x00400007, 0x600003C4)
        x = [3, 0xC2, 7, 0x1234] + [0] * 25
        struct.pack_into("<29Q", c, 8, *x)
        struct.pack_into("<4Q", c, 0xF0, S, LR, S, PC)
        return bytes(c)

    # wire --------------------------------------------------------------
    def send(self, ptype, payload, pid=None):
        if pid is None:
            pid = self.pid
            self.pid ^= 1
        b = kdb.frame_data(ptype, pid, payload)
        self.sent += b
        self.engine.feed(b)

    def raw(self, b):
        self.sent += b
        self.engine.feed(b)

    def on_host_bytes(self, b):
        for p in self.host_framer.feed(b):
            if isinstance(p, str):
                continue
            self.host_packets.append(p)
            if p.ctrl:
                continue
            self.raw(kdb.frame_ctrl(kdb.PT_ACK, p.pid))
            if p.ptype == kdb.PT_MANIPULATE:
                self.answer(p.payload)

    def answer(self, req):
        api, _, proc = struct.unpack_from("<IHH", req, 0)
        hdr = bytearray(req[:kdb.MANIPULATE_SIZE])
        data = b""
        if api == kdb.DbgKdContinueApi2:
            self.continues.append(struct.unpack_from("<I", req, 0x10)[0])
            return
        if api == kdb.DbgKdGetContextApi:
            data = self.context()
        elif api == kdb.DbgKdReadVirtualMemoryApi:
            va, n = struct.unpack_from("<QI", req, 0x10)
            data = self.read(va, n)
            struct.pack_into("<I", hdr, 0x1C, len(data))
            struct.pack_into("<I", hdr, 8, 0 if data else 0xC0000001)
        elif api == kdb.DbgKdGetVersionApi:
            struct.pack_into("<HHBBHH", hdr, 0x10, 0xF, 22621, 6, 2, 0x47, kdb.MACHINE_ARM64)
            struct.pack_into("<QQQ", hdr, 0x20, K, K + 0x500000, K + 0x600000)
        else:
            struct.pack_into("<I", hdr, 8, 0xC0000001)
        self.send(kdb.PT_MANIPULATE, bytes(hdr) + data)

    # target-side packet builders ---------------------------------------
    @staticmethod
    def state_change(state, proc=0, pc=0, thread=0xFFFF800011110080):
        p = bytearray(kdb.STATE_CHANGE_SIZE)
        struct.pack_into("<IHHI", p, 0, state, 0x0D03, proc, 8)
        struct.pack_into("<QQ", p, 0x10, thread, pc)
        return p

    def load(self, name, base, size):
        p = self.state_change(kdb.DbgKdLoadSymbolsStateChange)
        path = name.encode() + b"\0"
        struct.pack_into("<I", p, 0x20, len(path))
        struct.pack_into("<QQII", p, 0x28, base, 0, 0, size)
        return bytes(p) + path

    def exception(self, pc, code=kdb.STATUS_BREAKPOINT, proc=2):
        p = self.state_change(kdb.DbgKdExceptionStateChange, proc, pc)
        struct.pack_into("<IIQQI", p, 0x20, code, 0, 0, pc, 1)
        struct.pack_into("<Q", p, 0x48, 0xF000)
        struct.pack_into("<I", p, 0xB8, 1)
        struct.pack_into("<II", p, 0xC0, 0x600003C4, 16)
        p[0xC8:0xD8] = bytes.fromhex("00003ed4c0035fd6") + bytes(8)
        return bytes(p)

    @staticmethod
    def dbgprint(text):
        t = text.encode()
        return struct.pack("<IHHII", kdb.DbgKdPrintStringApi, 0, 0, len(t), 0) + t


def make_engine(target, **kw):
    log = kdb.Log(None, quiet=True, stamp=False)
    opts = kdb.Opts()
    for k, v in kw.items():
        setattr(opts, k, v)
    eng = kdb.Engine(target.on_host_bytes, log, opts)
    target.engine = eng
    return eng, log


def text(log):
    return "\n".join(log.lines)



class SerdPortTests(unittest.TestCase):
    def test_reads_appended_bytes_and_writes_the_fifo(self):
        with tempfile.TemporaryDirectory() as d:
            logp = os.path.join(d, "serd.log")
            fifo = os.path.join(d, "serd.in")
            with open(logp, "wb") as f:
                f.write(b"old firmware output")
            os.mkfifo(fifo)
            rfd = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)  # as serd.py does
            try:
                port = kdb.SerdPort(d)
                self.assertEqual(port.read(), b"")           # starts at the end
                with open(logp, "ab") as f:
                    f.write(b"\x30\x30\x30\x30new")
                self.assertEqual(port.read(), b"\x30\x30\x30\x30new")
                port.write(b"\x69\x69\x69\x69ack")
                self.assertEqual(os.read(rfd, 64), b"\x69\x69\x69\x69ack")
                port.close()
            finally:
                os.close(rfd)

class FramingTests(unittest.TestCase):
    def test_roundtrip_and_checksum(self):
        pl = bytes(range(200))
        b = kdb.frame_data(kdb.PT_DEBUG_IO, 0x80800001, pl)
        self.assertEqual(b[:4], b"0000")
        self.assertEqual(struct.unpack_from("<I", b, 12)[0], sum(pl))
        self.assertEqual(b[-1], 0xAA)
        f = kdb.Framer()
        got = [p for p in f.feed(b) if not isinstance(p, str)]
        self.assertEqual(len(got), 1)
        self.assertEqual(got[0].payload, pl)
        self.assertEqual(got[0].pid, 0x80800001)

    def test_bad_checksum_and_trailer_rejected(self):
        pl = b"hello world, this is a payload"
        good = kdb.frame_data(kdb.PT_DEBUG_IO, 0x80800000, pl)
        bad_ck = bytearray(good)
        bad_ck[20] ^= 1
        bad_tr = bytearray(good)
        bad_tr[-1] = 0xAB
        f = kdb.Framer()
        items = f.feed(bytes(bad_ck) + bytes(bad_tr) + good)
        pk = [p for p in items if not isinstance(p, str)]
        self.assertEqual(len(pk), 1)
        self.assertEqual(pk[0].payload, pl)

    def test_console_text_with_zeros_and_split_feeding(self):
        pl = b"x" * 50
        stream = (b"UEFI: wrote 0x00000000 to 0000 regs\r\n" +
                  kdb.frame_ctrl(kdb.PT_RESET) +
                  kdb.frame_data(kdb.PT_DEBUG_IO, 0x80800000, pl) +
                  b"after\n")
        f = kdb.Framer()
        items = []
        for i in range(len(stream)):
            items += f.feed(stream[i:i + 1])
        items += f.flush()
        pk = [p for p in items if not isinstance(p, str)]
        txt = [p for p in items if isinstance(p, str)]
        self.assertEqual([(p.ctrl, p.ptype) for p in pk],
                         [(True, kdb.PT_RESET), (False, kdb.PT_DEBUG_IO)])
        self.assertIn("UEFI: wrote 0x00000000 to 0000 regs", txt)
        self.assertIn("after", txt)

    def test_control_packet_needs_zero_length(self):
        f = kdb.Framer()
        bogus = b"iiii" + struct.pack("<HHII", kdb.PT_ACK, 5, 0, 0)
        items = f.feed(bogus + kdb.frame_ctrl(kdb.PT_ACK, 0x80800001))
        pk = [p for p in items if not isinstance(p, str)]
        self.assertEqual(len(pk), 1)
        self.assertEqual(pk[0].pid, 0x80800001)

    def test_call_insn(self):
        self.assertTrue(kdb.is_call_insn(0x94000010))      # bl
        self.assertTrue(kdb.is_call_insn(0xD63F0100))      # blr x8
        self.assertTrue(kdb.is_call_insn(0xD63F091F))      # blraaz x8
        self.assertFalse(kdb.is_call_insn(0xD65F03C0))     # ret
        self.assertFalse(kdb.is_call_insn(0x14000010))     # b


class ProtocolTests(unittest.TestCase):
    def test_sync_reset_ack_and_dedup(self):
        t = FakeTarget()
        eng, log = make_engine(t)
        ld = t.load(r"\SystemRoot\system32\ntoskrnl.exe", K, KSIZE)
        t.send(kdb.PT_STATE_CHANGE64, ld, pid=kdb.INITIAL_PACKET_ID | kdb.SYNC_PACKET_ID)
        self.assertEqual((t.host_packets[0].ctrl, t.host_packets[0].ptype), (True, kdb.PT_RESET))
        self.assertEqual(t.continues, [])            # the sync copy is not acted on
        t.send(kdb.PT_STATE_CHANGE64, ld, pid=kdb.INITIAL_PACKET_ID)
        t.send(kdb.PT_STATE_CHANGE64, ld, pid=kdb.INITIAL_PACKET_ID)   # retransmission
        acks = [p.pid for p in t.host_packets if p.ctrl and p.ptype == kdb.PT_ACK]
        self.assertEqual(acks, [kdb.INITIAL_PACKET_ID] * 2)
        self.assertEqual(t.continues, [kdb.DBG_CONTINUE])
        self.assertEqual(text(log).count("load \\SystemRoot\\system32\\ntoskrnl.exe"), 1)
        self.assertIn(f"0x{K:016x} 0x{KSIZE:x}", text(log))

    def test_user_mode_exception_not_handled(self):
        t = FakeTarget()
        eng, log = make_engine(t)
        t.send(kdb.PT_STATE_CHANGE64, t.exception(0x00007FF830000000))
        self.assertEqual(t.continues, [kdb.DBG_EXCEPTION_NOT_HANDLED])

    def test_prompt_answered_and_print(self):
        t = FakeTarget()
        eng, log = make_engine(t)
        t.send(kdb.PT_DEBUG_IO, t.dbgprint("hello from the kernel\n"))
        prompt = b"Break, Ignore (bi)? "
        t.send(kdb.PT_DEBUG_IO, struct.pack("<IHHII", kdb.DbgKdGetStringApi, 0, 1,
                                            len(prompt), 0) + prompt)
        self.assertIn("KD: hello from the kernel", text(log))
        reply = [p for p in t.host_packets if not p.ctrl][-1]
        self.assertEqual(reply.ptype, kdb.PT_DEBUG_IO)
        api, _, proc, _, n = struct.unpack_from("<IHHII", reply.payload, 0)
        self.assertEqual((api, proc, reply.payload[16:16 + n]), (kdb.DbgKdGetStringApi, 1, b"i"))

    def test_resend(self):
        t = FakeTarget()
        eng, log = make_engine(t)
        t.send(kdb.PT_STATE_CHANGE64, t.load("a.sys", D, DSIZE))
        n = len(t.continues)
        t.raw(kdb.frame_ctrl(kdb.PT_RESEND))
        self.assertEqual(len(t.continues), n + 1)


class BugcheckTests(unittest.TestCase):
    def run_bugcheck(self, **kw):
        t = FakeTarget(**kw)
        eng, log = make_engine(t)
        t.send(kdb.PT_STATE_CHANGE64, t.load(r"\SystemRoot\system32\ntoskrnl.exe", K, KSIZE))
        t.send(kdb.PT_STATE_CHANGE64, t.load(r"\SystemRoot\System32\drivers\mydrv.sys", D, DSIZE))
        t.send(kdb.PT_DEBUG_IO, t.dbgprint(
            "\n*** Fatal System Error: 0x000000c2\n"
            "                       (0x0000000000000007,0x0000000000001234,"
            "0x0000000000005678,0xffff800012345000)\n\n"))
        t.send(kdb.PT_STATE_CHANGE64, t.exception(PC))
        return t, eng, log

    def check_report(self, out, src="KiBugCheckData"):
        self.assertIn(f"bugcheck 0x000000c2 BAD_POOL_CALLER  [{src}]", out)
        self.assertIn("0x0000000000000007, 0x0000000000001234, 0x0000000000005678, "
                      "0xffff800012345000", out)
        self.assertIn("p1=0x7: pool freed twice", out)
        self.assertIn("x0 0x3 (DBG_STATUS_BUGCHECK_FIRST)", out)
        self.assertIn("ret 0xfffff80010001234 = mydrv.sys+0x1234", out)
        self.assertRegex(out, r"sp\+0x0048  0xfffff80010001234  fp call  mydrv.sys\+0x1234")
        self.assertRegex(out, r"sp\+0x0020  0xfffff80010008000           mydrv.sys\+0x8000")
        self.assertIn("SUSPECT: mydrv.sys+0x1234", out)

    def test_live_bugcheck(self):
        t, eng, log = self.run_bugcheck()
        out = text(log)
        self.check_report(out)
        self.assertIn("context: pc 0xfffff800002088b0 = ntoskrnl.exe+0x2088b0", out)
        self.assertIn("lr 0xfffff800004926f0 = ntoskrnl.exe+0x4926f0", out)
        self.assertIn("control report: cpsr 0x600003c4, 16 instruction bytes: 00 00 3e d4 c0 03 5f d6", out)
        self.assertIn("walked PsLoadedModuleList: 3 entries", out)
        self.assertIn(f"KDBG at 0x{K + 0x610000:016x}", out)
        # the stack stopped at the end of the mapped region
        self.assertIn(f"stack scan: {SSIZE} bytes", out)
        # exactly one continue for the bugcheck, after all reads, DBG_CONTINUE at 0x10
        self.assertEqual(t.continues, [kdb.DBG_CONTINUE] * 3)
        last = [p for p in t.host_packets if not p.ctrl][-1]
        self.assertEqual(struct.unpack_from("<I", last.payload, 0)[0], kdb.DbgKdContinueApi2)
        self.assertEqual(eng.flow, None)
        self.assertEqual(len(eng.reports), 1)
        rep = eng.reports[0]
        self.assertTrue(rep["modules"].lookup(X + 0x10))      # added by the module walk

    def test_offline_replay_matches_live(self):
        t, eng, log = self.run_bugcheck()
        with tempfile.TemporaryDirectory() as d:
            raw = os.path.join(d, "cap.bin")
            outlog = os.path.join(d, "cap.log")
            with open(raw, "wb") as f:
                f.write(t.sent)
            import io
            import contextlib
            with contextlib.redirect_stdout(io.StringIO()):
                kdb.main(["--offline", raw, "--log", outlog])
            with open(outlog) as f:
                out = f.read()
        self.check_report(out)
        self.assertIn("1 report(s)", out)

    def test_encoded_kdbg_falls_back_to_print(self):
        t, eng, log = self.run_bugcheck(kdbg_tag=0x12345678)
        out = text(log)
        self.assertIn("not 'KDBG'", out)
        self.check_report(out, src="Fatal System Error print")

    def test_lost_reply_times_out(self):
        t = FakeTarget()
        eng, log = make_engine(t)
        now = [0.0]
        eng.clock = lambda: now[0]
        t.send(kdb.PT_STATE_CHANGE64, t.load(r"\SystemRoot\system32\ntoskrnl.exe", K, KSIZE))
        t.send(kdb.PT_DEBUG_IO, t.dbgprint("*** Fatal System Error: 0x000000c2\n"))
        t.answer = lambda req: None                           # target goes silent
        t.send(kdb.PT_STATE_CHANGE64, t.exception(PC))
        for _ in range(20):
            now[0] += 6
            eng.tick()
            if eng.flow is None:
                break
        self.assertIsNone(eng.flow)
        self.assertIn("no register context", text(log))


if __name__ == "__main__":
    unittest.main(verbosity=2)

# scripts

## kd-bugcheck.py: name the driver behind a Windows bugcheck

For a bugcheck that leaves no crash dump. It speaks just enough of the
Windows kernel debugger protocol (KDCOM) on the debug UART to keep the boot
going. When the kernel bugchecks, it reads the registers, the stack and the
bugcheck data, then prints the call chain as `module+offset`. You do not need
WinDbg. Needs Python 3, and pyserial if you have it (`python3-serial`).

### On the board (Windows, admin prompt), once

```
bcdedit /debug on
bcdedit /dbgsettings serial debugport:1 baudrate:1500000
```

`debugport:1` is the first port in the firmware's DBG2 table, UART0 at
0x2AD40000, the same port as the UEFI console. The line runs at the rate UEFI
set, 1500000. SPCR carries BaudRate 0, so Windows keeps the port as it finds
it, and earlier captures ran at 1500000 while the BCD said 115200. Setting
1500000 here keeps the two in agreement. Leave `bootdebug` off. With it on,
winload asks the debugger for every boot file. The tool refuses each request,
and the boot gets slower.

If Windows no longer boots, edit the BCD from another Windows or WinPE:
`bcdedit /store X:\EFI\Microsoft\Boot\BCD /set {default} debug on`.

### On the host, before powering the board

Stop every other reader of the probe first: `serial-log*.sh`,
`kd-resilient.sh`, `kd-listen.py`, screen, minicom. If two programs read
one tty, each gets part of the stream and neither copy is usable. The tool
refuses to start if another process has the port open.

```
python3 scripts/kd-bugcheck.py /dev/ttyACM0 --out-dir ~/Desktop/woa-kd-captures
```

(`auto` in place of the device finds the Raspberry Pi Debug Probe by USB id.)
It writes `kd-bugcheck-<time>.log`, the received bytes `.bin` and the sent
bytes `-tx.bin`. It reattaches if the probe re-enumerates. Ctrl-C stops it.

### At a bugcheck

```
KD: *** Fatal System Error: 0x000000c2
KD:                        (0x0000000000000007,...)
EXCEPTION 0x80000003 at 0xfffff800bc2088b0 = ntoskrnl.exe+0x2088b0 proc 2 ...
  [dump] reading context, stack, KiBugCheckData of proc 2
bugcheck 0x000000c2 BAD_POOL_CALLER  [KiBugCheckData]
  params 0x...7, 0x..., 0x..., 0x...
context: pc ... lr ... sp ... fp ...  x0 0x3 (DBG_STATUS_BUGCHECK_FIRST) x1 x2 x3
frame-pointer chain (N frames):
  #01 fp 0xffff... ret 0xfffff800... = ntoskrnl.exe+0x...
  #05 fp 0xffff... ret 0xfffff800... = somedrv.sys+0x1234
stack scan: 16384 bytes ... ('fp' / 'call' marks)
SUSPECT: somedrv.sys+0x1234  (first non-kernel return address, from the frame-pointer chain)
  [dump] done, continued proc 2
```

The offset is relative to the image base. Look it up in that driver's PDB or
map file. The board stays in the debugger only while the tool reads, then the
tool lets it continue. The kernel may break in again for the same bugcheck
(`x0` = BUGCHECK_SECOND). Those breaks are continued without a new dump.

Also: `--dump-exceptions` dumps on any kernel exception that is not a
breakpoint, and `--dump-breaks` on every kernel breakpoint.

### Afterwards

`bcdedit /debug off` on the board. With debugging on and no listener, the boot
crawls and USB storage can time out.

### Offline and tests

```
python3 scripts/kd-bugcheck.py --offline kd-bugcheck-<time>.bin   # re-analyse a capture
python3 scripts/test_kd_bugcheck.py                               # host-only unit tests
```

Offline mode also reads old `kd-listen.py` captures (`kd-raw-*.bin`). The
structure offsets the tool relies on, and their sources, are listed at the top
of `kd-bugcheck.py`.

#!/usr/bin/env python3
#
# What a firmware build supports, read from the build output.
#
#   scripts/features.py board <board>
#       After scripts/package.sh <board>: writes out/<PLATFORM>/features.json
#       from Build/<PLATFORM>/RELEASE_GCC (the FFS files in FVMAIN.Fv, the
#       FixedAtBuild PCD values in the modules' AutoGen, the compiled setup
#       forms) and out/<PLATFORM>/spl.config.  Fails when a feature that
#       boards/<board>.conf lists in FEATURES_EXPECTED is not in the build.
#
#   scripts/features.py release --tag <tag> [--asset-version <v>] --out <dir> <input>...
#       For a release: finds every features.json under the inputs (the
#       artifacts of a build run, from `gh run download`, or out/), copies
#       each board's images to <dir> as <Board>-<medium>-edk2-<v>.img with
#       the .sfdisk tables, writes <dir>/FEATURES.json, <dir>/SHA256SUMS.txt
#       and features-notes.md (the table for the release notes) next to <dir>.
#       Fails when a board's images are not the ones its features.json was
#       made for.
#
#   scripts/features.py notes <FEATURES.json>
#       Prints the release notes table.
#
# docs/dev/features-json.md describes the files.

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import uuid

SCHEMA = 1            # features.json and FEATURES.json
SETTINGS_SCHEMA = 1   # the "settings" section

ACPI_TABLES_GUID = '7e374e25-8e01-4fee-87f2-390c23c606cd'
PLATFORM_SETUP_CLASS = '93039971-8545-4b04-b45e-32eb8326040e'   # listed under Device Manager
FRONT_PAGE_CLASS = 'e58809f8-fbc1-48e2-883a-a30fdc4b441e'       # gEfiIfrFrontPageGuid: on the front page

# (key, label for the notes table)
FEATURES = [
    ('windows', 'Windows (ACPI tables)'),
    ('armbianOnce', 'Armbian (U-Boot) once'),
    ('hdmi', 'HDMI output'),
    ('dsi', 'MIPI DSI screen'),
    ('usbcDp', 'USB-C DisplayPort'),
    ('cpuIdle', 'CPU idle states'),
    ('fanControl', 'Fan control'),
    ('networkBoot', 'Network boot (PXE)'),
    ('httpBoot', 'HTTP boot'),
    ('secureBoot', 'Secure Boot'),
]

# A question whose default is another variable ("default value =
# get(DisplayModeDefault.Preset)"): RK3576Dxe fills that variable at boot
# from this PCD (Display.c), so the PCD holds the default.
DEFAULT_FROM_PCD = {
    'DisplayModeDefault': 'PcdDisplayModePresetDefault',
    'DisplayModeCustomDefault': 'PcdDisplayModeCustomDefault',
}


def die(msg):
    print(f'features.py: {msg}', file=sys.stderr)
    sys.exit(1)


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def read_conf(path):
    conf = {}
    for line in open(path):
        m = re.match(r'^([A-Z0-9_]+)=(.*)$', line.strip())
        if m:
            conf[m.group(1)] = m.group(2).strip().strip('"')
    return conf


# ---------------------------------------------------------------- FV files

def guid_str(b):
    return str(uuid.UUID(bytes_le=bytes(b)))


def fv_files(path):
    """The FFS file GUIDs of a firmware volume, in order."""
    d = open(path, 'rb').read()
    if d[40:44] != b'_FVH':
        die(f'{path} is not a firmware volume')
    fv_len = struct.unpack_from('<Q', d, 32)[0]
    hdr_len = struct.unpack_from('<H', d, 48)[0]
    ext = struct.unpack_from('<H', d, 52)[0]
    off = hdr_len
    if ext:
        off = ext + struct.unpack_from('<I', d, ext + 16)[0]
    out = []
    while True:
        off = (off + 7) & ~7
        if off + 24 > fv_len:
            break
        name = d[off:off + 16]
        if name == b'\xff' * 16:
            break
        attr = d[off + 19]
        size = int.from_bytes(d[off + 20:off + 23], 'little')
        if attr & 0x01:   # FFS_ATTRIB_LARGE_FILE
            size = struct.unpack_from('<Q', d, off + 24)[0]
        if size < 24:
            break
        if d[off + 18] != 0xF0:   # not a pad file
            out.append(guid_str(name))
        off += size
    return out


def guid_names(xref):
    names = {}
    for line in open(xref):
        p = line.split()
        if len(p) == 2 and re.match(r'^[0-9a-fA-F-]{36}$', p[0]):
            names.setdefault(p[0].lower(), p[1])
    return names


# ---------------------------------------------------------------- PCDs

def parse_int(s):
    s = s.strip()
    s = re.sub(r'\(\s*(BOOLEAN|UINT8|UINT16|UINT32|UINT64)\s*\)', '', s)
    s = s.strip('() ')
    m = re.match(r'^(0x[0-9a-fA-F]+|\d+)(U|ULL|UL)?$', s)
    return int(m.group(1), 0) if m else None


def fixed_pcds(arch_dir, modules):
    """FixedAtBuild PCD values: name -> int or bytes, from the AutoGen files
    of the modules in the FV.  Two modules disagreeing is an error."""
    vals, where = {}, {}
    for root, dirs, files in os.walk(arch_dir):
        if os.path.basename(root) != 'DEBUG' or 'AutoGen.h' not in files:
            continue
        mod = os.path.basename(os.path.dirname(root))
        if mod not in modules:
            continue
        h = open(os.path.join(root, 'AutoGen.h'), errors='replace').read()
        c = open(os.path.join(root, 'AutoGen.c'), errors='replace').read() if 'AutoGen.c' in files else ''
        arrays = {m.group(1): bytes(int(x, 0) for x in m.group(2).replace(' ', '').split(',') if x)
                  for m in re.finditer(r'const UINT8 _gPcd_FixedAtBuild_(\w+)\[\d+\] = \{([^}]*)\}', c)}
        for m in re.finditer(r'^#define _PCD_VALUE_(\w+)\s+(.+)$', h, re.M):
            name, v = m.group(1), m.group(2).strip()
            if '_gPcd_FixedAtBuild_' in v:
                val = arrays.get(name)
            else:
                val = parse_int(v)
            if val is None:
                continue
            if name in vals and vals[name] != val:
                die(f'{name} is {vals[name]!r} in {where[name]} but {val!r} in {mod}')
            vals[name], where[name] = val, mod
    return vals


# ---------------------------------------------------------------- HII

def c_array_bytes(path, name_suffix):
    t = open(path, errors='replace').read()
    m = re.search(r'unsigned char (\w+' + name_suffix + r')\[\] = \{(.*?)\};', t, re.S)
    if not m:
        return None
    body = re.sub(r'//[^\n]*', '', m.group(2))
    return bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body))


def string_packages(path):
    """{language: {string id: text}} from a StrDefs.hpk."""
    d = open(path, 'rb').read()
    out, o = {}, 0
    while o + 4 <= len(d):
        h = struct.unpack_from('<I', d, o)[0]
        ln, typ = h & 0xFFFFFF, h >> 24
        if ln == 0:
            break
        if typ == 0x04:
            out.update(_strings(d[o:o + ln]))
        o += ln
    return out


def _strings(p):
    hdr = struct.unpack_from('<I', p, 4)[0]
    info = struct.unpack_from('<I', p, 8)[0]
    lang = p[46:p.index(b'\0', 46)].decode()
    s, sid, o = {}, 1, info

    def ucs2(o):
        e = o
        while p[e:e + 2] != b'\0\0':
            e += 2
        return p[o:e].decode('utf-16-le'), e + 2

    while o < len(p):
        t = p[o]
        if t == 0x00:      # END
            break
        elif t == 0x14:    # STRING_UCS2
            s[sid], o = ucs2(o + 1)
            sid += 1
        elif t == 0x15:    # STRING_UCS2_FONT
            s[sid], o = ucs2(o + 2)
            sid += 1
        elif t == 0x16:    # STRINGS_UCS2
            n = struct.unpack_from('<H', p, o + 1)[0]
            o += 3
            for _ in range(n):
                s[sid], o = ucs2(o)
                sid += 1
        elif t == 0x17:    # STRINGS_UCS2_FONT
            n = struct.unpack_from('<H', p, o + 2)[0]
            o += 4
            for _ in range(n):
                s[sid], o = ucs2(o)
                sid += 1
        elif t == 0x20:    # DUPLICATE
            s[sid] = s.get(struct.unpack_from('<H', p, o + 1)[0], '')
            sid += 1
            o += 3
        elif t == 0x21:    # SKIP2
            sid += struct.unpack_from('<H', p, o + 1)[0]
            o += 3
        elif t == 0x22:    # SKIP1
            sid += p[o + 1]
            o += 2
        elif t == 0x30:    # EXT1
            o += p[o + 2]
        elif t == 0x31:    # EXT2
            o += struct.unpack_from('<H', p, o + 2)[0]
        elif t == 0x32:    # EXT4
            o += struct.unpack_from('<I', p, o + 2)[0]
        elif t == 0x40:    # FONT
            o += struct.unpack_from('<H', p, o + 2)[0]
        else:
            die(f'unknown string block 0x{t:02x}')
    # \wide and \narrow in the .uni files are U+FFF1 and U+FFF2.
    clean = {k: re.sub('[￰-￲]', '', v) for k, v in s.items()}
    return {lang: clean}


SIZES = {0: 1, 1: 2, 2: 4, 3: 8}


def attr_names(a):
    return [n for bit, n in ((1, 'NV'), (2, 'BS'), (4, 'RT'), (8, 'RO')) if a & bit]


def decode_ifr(ifr, strings, module):
    """Questions on EFI variable stores in one compiled formset."""
    def text(sid):
        r = {lang: tbl.get(sid) for lang, tbl in strings.items() if tbl.get(sid)}
        return r or None

    def en(sid):
        t = text(sid) or {}
        return t.get('en-US') or next(iter(t.values()), None)

    varstores, forms, refs, questions, omitted = {}, {}, [], [], []
    formset = None
    stack = []          # (opcode, payload) for each open scope
    form = None
    q = None            # the question whose scope is open
    cond = []           # the expression opcodes after the last suppressif
    o = 0
    while o < len(ifr):
        op = ifr[o]
        ln = ifr[o + 1] & 0x7F
        scope = ifr[o + 1] & 0x80
        b = ifr[o:o + ln]
        if ln < 2:
            die(f'{module}: bad IFR at {o}')
        item = None
        in_expr = bool(stack) and stack[-1][0] == 'expr'
        if not is_expr(op) and not (op == 0x29 and in_expr):
            for k, p in stack:
                if k in ('suppress', 'grayout'):
                    p['open'] = False
        if op == 0x0E:      # FORM_SET
            classes = [guid_str(b[23 + 16 * i:39 + 16 * i]) for i in range(b[22] & 3)]
            formset = {'guid': guid_str(b[2:18]), 'title': en(struct.unpack_from('<H', b, 18)[0]),
                       'setup': PLATFORM_SETUP_CLASS in classes, 'frontPage': FRONT_PAGE_CLASS in classes}
        elif op == 0x26:    # VARSTORE_EFI
            vid = struct.unpack_from('<H', b, 2)[0]
            name = b[26:b.index(b'\0', 26)].decode()
            varstores[vid] = {'name': name, 'guid': guid_str(b[4:20]),
                              'attributes': struct.unpack_from('<I', b, 20)[0],
                              'size': struct.unpack_from('<H', b, 24)[0]}
        elif op == 0x24:    # VARSTORE (buffer, through a driver's config access)
            vid = struct.unpack_from('<H', b, 18)[0]
            varstores[vid] = None
        elif op == 0x01:    # FORM
            fid, title = struct.unpack_from('<HH', b, 2)
            form = fid
            forms[fid] = en(title)
            item = ('form', fid)
        elif op == 0x0F and form is not None:   # REF (goto)
            if ln >= 15:
                to_formset = guid_str(b[17:33]) if ln >= 33 else None
                if to_formset is None or (formset and to_formset == formset['guid']):
                    refs.append((form, struct.unpack_from('<H', b, 13)[0]))
        elif op in (0x05, 0x06, 0x07, 0x1C, 0x23):   # ONE_OF CHECKBOX NUMERIC STRING ORDERED_LIST
            prompt, _help, qid, vid, voff, qflags = struct.unpack_from('<HHHHHB', b, 2)
            kind = {0x05: 'oneof', 0x06: 'checkbox', 0x07: 'numeric', 0x1C: 'string', 0x23: 'orderedlist'}[op]
            q = {'kind': kind, 'prompt_id': prompt, 'vid': vid, 'offset': voff, 'form': form,
                 'reset': bool(qflags & 0x10), 'readonly': bool(qflags & 0x01), 'options': [],
                 'suppress': [p['ops'] for k, p in stack if k == 'suppress'],
                 'grayout': [p['ops'] for k, p in stack if k == 'grayout']}
            if op in (0x05, 0x07):
                f = b[13]
                w = SIZES[f & 0x0F]
                q['width'] = w
                q['min'], q['max'], q['step'] = struct.unpack_from('<' + 'BHIQ'[[1, 2, 4, 8].index(w)] * 3, b, 14)
                q['hex'] = (f & 0x30) == 0x20
            elif op == 0x06:
                q['width'] = 1
                if b[13] & 0x01:
                    q['default'] = 1
                else:
                    q.setdefault('default', 0)
            elif op == 0x1C:
                q['minSize'], q['maxSize'] = b[13], b[14]
                q['width'] = b[14] * 2
            elif op == 0x23:
                q['containers'] = b[13]
            questions.append(q)
            item = ('q', q)
        elif op == 0x09 and q is not None:   # ONE_OF_OPTION
            sid, flags, typ = struct.unpack_from('<HBB', b, 2)
            if typ <= 3:
                v = int.from_bytes(b[6:6 + SIZES[typ]], 'little')
            elif typ == 4:
                v = b[6]
            else:
                v = None
            q['options'].append({'value': v, 'label': text(sid)})
            if q['kind'] == 'orderedlist' and typ <= 3:
                q['elem'] = SIZES[typ]
            if flags & 0x10 and v is not None:
                q['default'] = v
        elif op == 0x5B and q is not None:   # DEFAULT
            did, typ = struct.unpack_from('<HB', b, 2)
            if did == 0:
                if typ <= 3:
                    q['default'] = int.from_bytes(b[5:5 + SIZES[typ]], 'little')
                elif typ == 4:
                    q['default'] = b[5]
                elif typ == 0x08 and scope:   # OTHER: an expression follows
                    q['default_expr'] = []
            item = ('default', None)
        elif op == 0x2B and q is not None and 'default_expr' in q and not q.get('default_done'):   # GET
            vid, voff, vtyp = struct.unpack_from('<HHB', b, 2)
            q['default_expr'].append((vid, voff, vtyp))
        elif op == 0x0A:    # SUPPRESS_IF
            item = ('suppress', {'ops': [], 'open': True})
        elif op == 0x19:    # GRAY_OUT_IF
            item = ('grayout', {'ops': [], 'open': True})
        elif is_expr(op):
            cond = next((p for k, p in reversed(stack) if k != 'expr'), None) if stack else None
            if cond is not None and isinstance(cond, dict) and cond.get('open'):
                cond['ops'].append(b)
            item = ('expr', None)
        elif op == 0x29:    # END
            if stack:
                kind, payload = stack.pop()
                if kind == 'q':
                    q = None
                elif kind == 'form':
                    form = None
                elif kind == 'default' and q is not None and 'default_expr' in q:
                    q['default_done'] = True
            o += ln
            continue
        if scope:
            stack.append(item if item else ('other', None))
        o += ln

    return formset, varstores, forms, refs, questions


def is_expr(op):
    return 0x12 <= op <= 0x18 or 0x20 <= op <= 0x22 or op in (0x2A, 0x2B, 0x64) or (0x2D <= op <= 0x5A and op != 0x3F)


def expr_text(ops, varstores, qids):
    """An IFR expression (postfix) as text, or None when it uses an opcode
    this does not know."""
    st = []
    bin_ops = {0x15: 'and', 0x16: 'or', 0x2F: '==', 0x30: '!=', 0x31: '>', 0x32: '>=', 0x33: '<', 0x34: '<=',
               0x35: '&', 0x36: '|', 0x3A: '+', 0x3B: '-'}
    try:
        for b in ops:
            op = b[0]
            if op == 0x12:      # EQ_ID_VAL
                qid, val = struct.unpack_from('<HH', b, 2)
                v = qids.get(qid)
                if not v:
                    return None
                st.append(f'{v[0]}@{v[1]} == {val}')
            elif op == 0x2B:    # GET
                vid, voff = struct.unpack_from('<HH', b, 2)
                vs = varstores.get(vid)
                if not vs:
                    return None
                st.append(f'{vs["name"]}@{voff}')
            elif op in (0x42, 0x43, 0x44, 0x45):
                st.append(str(int.from_bytes(b[2:2 + (1, 2, 4, 8)[op - 0x42]], 'little')))
            elif op == 0x46:
                st.append('true')
            elif op == 0x47:
                st.append('false')
            elif op == 0x52:
                st.append('0')
            elif op == 0x53:
                st.append('1')
            elif op == 0x17:
                st.append(f'not ({st.pop()})')
            elif op in bin_ops:
                r, l = st.pop(), st.pop()
                st.append(f'({l} {bin_ops[op]} {r})')
            else:
                return None
    except IndexError:
        return None
    if len(st) != 1:
        return None
    t = st[0]
    return t[1:-1] if t.startswith('(') and t.endswith(')') and t.count('(') == 1 else t


def settings_from_formset(formset, varstores, forms, refs, questions, pcds, module, strings_qid):
    parent = {}
    for frm, to in refs:
        if to != frm and to not in parent:
            parent[to] = frm

    def path(fid):
        chain = []
        seen = set()
        while fid is not None and fid not in seen:
            seen.add(fid)
            chain.append(forms.get(fid))
            fid = parent.get(fid)
        chain.reverse()
        # A front page formset is a top-level entry (UiApp lists it there,
        # and also under Device Manager when it has both classes).
        top = ['Device Manager'] if formset['setup'] and not formset['frontPage'] else []
        if not chain or chain[0] != formset['title']:
            top.append(formset['title'])
        return top + [c for c in chain if c]

    out, omitted = [], []
    for q in questions:
        if q['prompt_id'] == 0:
            continue   # made by the VFR compiler for "ideqval Var == n"; not on any page
        vs = varstores.get(q['vid'])
        label = q['prompt_text']
        if not vs:
            omitted.append({'prompt': label, 'module': module,
                            'reason': 'not stored in an EFI variable of its own (a driver handles it through HII config access)'
                            if q['vid'] in varstores else 'not stored in a variable'})
            continue
        if q['kind'] == 'orderedlist':
            q['width'] = q['containers'] * q.get('elem', 1)
        default = q.get('default')
        source = 'form'
        if q.get('default_expr'):
            (dvid, doff, _t), = q['default_expr'][:1]
            dvs = varstores.get(dvid)
            pcd = DEFAULT_FROM_PCD.get(dvs['name']) if dvs else None
            val = pcds.get(pcd) if pcd else None
            if isinstance(val, bytes) and doff + q['width'] <= len(val):
                default = int.from_bytes(val[doff:doff + q['width']], 'little')
                source = f'{pcd} (through the variable {dvs["name"]})'
            else:
                default = None
        if default is None and q['kind'] not in ('string',):
            omitted.append({'prompt': label, 'variable': vs['name'], 'module': module,
                            'reason': 'its default could not be read from the build'})
            continue
        s = {
            'id': vs['name'] if q['offset'] == 0 and q['width'] == vs['size'] else f'{vs["name"]}@{q["offset"]}',
            'prompt': q['prompt'],
            'menu': path(q['form']),
            'variable': {'name': vs['name'], 'guid': vs['guid'], 'attributes': vs['attributes'],
                         'attributeNames': attr_names(vs['attributes']), 'size': vs['size']},
            'standalone': q['offset'] == 0 and q['width'] == vs['size'],
            'offset': q['offset'],
            'width': q['width'],
            'type': q['kind'],
            'resetRequired': q['reset'],
        }
        if q['kind'] == 'string':
            s['minChars'], s['maxChars'] = q['minSize'], q['maxSize']
            s['encoding'] = 'UCS-2, NUL-terminated'
            s['default'] = ''
            source = 'empty string (forms have no default for strings)'
        else:
            s['default'] = default
        s['defaultSource'] = source
        if q['kind'] in ('oneof', 'orderedlist'):
            s['values'] = q['options']
        elif q['kind'] == 'checkbox':
            s['values'] = [{'value': 0, 'label': {'en-US': 'Disabled'}}, {'value': 1, 'label': {'en-US': 'Enabled'}}]
        elif q['kind'] == 'numeric':
            s['min'], s['max'], s['step'] = q['min'], q['max'], q['step']
        if q['kind'] == 'orderedlist':
            s['elementWidth'] = q.get('elem', 1)
            s['containers'] = q['containers']
        for key, groups in (('hiddenWhen', q['suppress']), ('grayedOutWhen', q['grayout'])):
            conds = [expr_text(g, varstores, strings_qid) or 'expression not decoded' for g in groups if g]
            if conds:
                s[key] = conds
        # "<X>Supported" variables are written at boot by RK3576Dxe from
        # Pcd<X>Supported where there is one; some are probed at boot.
        for c in s.get('hiddenWhen', []):
            m = re.fullmatch(r'(\w+Supported)@0 == 0', c)
            if m:
                v = pcds.get('Pcd' + m.group(1))
                s['boardSupport'] = {'variable': m.group(1), 'pcd': 'Pcd' + m.group(1), 'value': bool(v)} if v is not None \
                    else {'variable': m.group(1), 'pcd': None, 'value': None}
        out.append(s)
    return out, omitted


def hii_settings(arch_dir, modules, pcds, stores=None):
    """The settings of the compiled setup forms.  With stores, also appends
    (module, module source directory, efivarstore) for every efivarstore."""
    settings, omitted = [], []
    for root, dirs, files in sorted(os.walk(arch_dir)):
        if os.path.basename(root) != 'DEBUG':
            continue
        moddir = os.path.dirname(root)
        mod = os.path.basename(moddir)
        if mod not in modules:
            continue
        for f in sorted(files):
            if not f.endswith('.c') or f == 'AutoGen.c':
                continue
            p = os.path.join(root, f)
            with open(p, errors='replace') as fh:
                if 'vfrcompiler utility' not in fh.read(300):
                    continue
            data = c_array_bytes(p, 'Bin')
            if not data:
                continue
            ifr = data[8:]   # array length, package header
            outdir = os.path.join(moddir, 'OUTPUT')
            hpk = [x for x in os.listdir(outdir) if x.endswith('StrDefs.hpk')] if os.path.isdir(outdir) else []
            strings = string_packages(os.path.join(outdir, hpk[0])) if hpk else {}
            formset, varstores, forms, refs, questions = decode_ifr(ifr, strings, mod)
            if not formset:
                continue
            if stores is not None:
                src = os.path.relpath(os.path.dirname(moddir), arch_dir)
                stores += [(mod, src, v) for v in varstores.values() if v]
            for q in questions:
                t = {lang: tbl.get(q['prompt_id']) for lang, tbl in strings.items() if tbl.get(q['prompt_id'])}
                q['prompt'] = t
                q['prompt_text'] = t.get('en-US') or f'question {q["prompt_id"]}'
            qid = qid_map(ifr, varstores)
            s, om = settings_from_formset(formset, varstores, forms, refs, questions, pcds, mod, qid)
            for x in s:
                x['module'] = mod
                x['formset'] = formset['guid']
            settings += s
            omitted += om
    return settings, omitted


def qid_map(ifr, varstores):
    """Question id -> (variable name, offset), including the questions the
    VFR compiler makes for "ideqval Var == n" on a variable with no question."""
    m, o = {}, 0
    while o < len(ifr):
        op, ln = ifr[o], ifr[o + 1] & 0x7F
        if op in (0x05, 0x06, 0x07, 0x1C, 0x23, 0x0F) and ln >= 13:
            _p, _h, qid, vid, voff = struct.unpack_from('<HHHHH', ifr, o + 2)
            vs = varstores.get(vid)
            if vs:
                m[qid] = (vs['name'], voff)
        o += ln
    return m


# ---------------------------------------- setup variables that are created

def _c_calls(text, func):
    """The argument lists of every call of func in C text, split at the
    top-level commas."""
    calls = []
    for m in re.finditer(r'\b' + func + r'\s*\(', text):
        depth, args, cur, i = 1, [], '', m.end()
        while i < len(text) and depth:
            c = text[i]
            if c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
                if not depth:
                    break
            if c == ',' and depth == 1:
                args.append(cur.strip())
                cur = ''
            else:
                cur += c
            i += 1
        args.append(cur.strip())
        calls.append(args)
    return calls


def created_variables(dirs, dsc_dirs):
    """Names of the variables some C source in dirs creates: a SetVariable
    whose name is L"..." (or a macro for one) that is not a delete (0, 0), or
    a PcdSet*S of a DynamicHii PCD bound to the name in a .dsc in dsc_dirs
    (PcdDxe creates a missing HII variable on a set)."""
    sources = []
    for d in dirs:
        for r, _ds, fs in os.walk(d):
            sources += [os.path.join(r, f) for f in fs if f.endswith(('.c', '.h'))]
    texts = [open(p, errors='replace').read() for p in sorted(set(sources))]
    macros = {}
    for t in texts:
        for m in re.finditer(r'^\s*#\s*define\s+(\w+)\s+L"(\w+)"', t, re.M):
            macros[m.group(1)] = m.group(2)
    hii = {}
    for d in dsc_dirs:
        for r, _ds, fs in os.walk(d):
            for f in fs:
                if f.endswith(('.dsc', '.dsc.inc')):
                    for m in re.finditer(r'\.(Pcd\w+)\|L"(\w+)"\|', open(os.path.join(r, f), errors='replace').read()):
                        hii[m.group(1)] = m.group(2)
    names = set()
    for t in texts:
        for a in _c_calls(t, 'SetVariable'):
            if len(a) >= 4 and a[2] == '0' and a[3] == '0':
                continue
            m = re.fullmatch(r'L"(\w+)"', a[0])
            n = m.group(1) if m else macros.get(a[0])
            if n:
                names.add(n)
        for m in re.finditer(r'\bPcdSet\w*S\s*\(\s*(Pcd\w+)', t):
            if m.group(1) in hii:
                names.add(hii[m.group(1)])
    return names


# Variables whose SetVariable takes the name from a table, which
# created_variables cannot follow: name -> the file with the table.  Each
# must still be in that file.
CREATED_FROM_TABLE = {
    'FdtCompatModeVendorSupported': 'Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/ConfigTable.c',
    'FdtCompatModeMainlineSupported': 'Silicon/Rockchip/RK3576/Drivers/RK3576Dxe/ConfigTable.c',
}


def check_setup_variables(root, stores):
    """Every efivarstore of a compiled setup form needs code that creates
    the variable: the setup browser saves an efivarstore only when the
    variable exists (HiiDatabaseDxe RouteConfigRespForEfiVarStore), so one
    that is never created fails every F10 in its formset with "The variable
    does not exist!".  Returns the stores nothing creates."""
    edk2 = os.environ.get('EDK2_DIR') or os.path.join(root, 'third_party', 'edk2')
    deps = os.environ.get('DEPS_DIR') or os.path.join(root, 'third_party')
    trees = [root, edk2] + [os.path.join(deps, d) for d in ('edk2-non-osi', 'edk2-platforms', 'edk2-rockchip-non-osi')]
    ours = [os.path.join(root, 'Silicon'), os.path.join(root, 'Platform')]
    created = {}
    missing = []
    for mod, src, vs in stores:
        name = vs['name']
        if name in CREATED_FROM_TABLE:
            p = os.path.join(root, CREATED_FROM_TABLE[name])
            if os.path.isfile(p) and f'L"{name}"' in open(p, errors='replace').read():
                continue
        srcdir = next((os.path.join(t, src) for t in trees if os.path.isdir(os.path.join(t, src))), None)
        if srcdir is None:
            missing.append(f'{name} ({mod}: source {src} not found)')
            continue
        if srcdir not in created:
            created[srcdir] = created_variables([srcdir] + ours, ours)
        if name not in created[srcdir]:
            missing.append(f'{name} ({mod}, {src})')
    return missing


# ---------------------------------------------------------------- board

def find_image_fv(img, fv_bytes):
    with open(img, 'rb') as f:
        return f.read().find(fv_bytes) >= 0


def board(args):
    root = args.root
    conf_path = os.path.join(root, 'boards', f'{args.board}.conf')
    if not os.path.isfile(conf_path):
        die(f'no board config {conf_path}')
    conf = read_conf(conf_path)
    plat = conf['PLATFORM_NAME']
    build = os.path.join(root, 'Build', plat, 'RELEASE_GCC')
    fvdir = os.path.join(build, 'FV')
    outdir = os.path.join(root, 'out', plat)
    for p in (os.path.join(fvdir, 'FVMAIN.Fv'), os.path.join(fvdir, 'BL33_AP_UEFI.Fv'), os.path.join(fvdir, 'Guid.xref')):
        if not os.path.isfile(p):
            die(f'{p} is missing; run scripts/build.sh {args.board} first')

    names = guid_names(os.path.join(fvdir, 'Guid.xref'))
    guids = fv_files(os.path.join(fvdir, 'FVMAIN.Fv'))
    modules = {names.get(g, g) for g in guids}
    has = lambda m: m in modules
    pcds = fixed_pcds(os.path.join(build, 'AARCH64'), modules)
    pcd = lambda n, d=None: pcds.get(n, d)

    fvmain = open(os.path.join(fvdir, 'FVMAIN.Fv'), 'rb').read()
    m = re.search(('rk3576-' + plat + '-').encode('utf-16-le') + rb'(?:[\x20-\x7e]\x00)+', fvmain)
    version = m.group(0).decode('utf-16-le') if m else None
    if not version:
        die('no firmware version string (rk3576-<PLATFORM>-...) in FVMAIN.Fv')

    spl_path = os.path.join(outdir, 'spl.config')
    spl = {}
    if os.path.isfile(spl_path):
        for line in open(spl_path):
            mm = re.match(r'^(CONFIG_\w+)=(.*)$', line.strip())
            if mm:
                spl[mm.group(1)] = mm.group(2)
    spl_once = spl.get('CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE') == 'y'
    spl_sd = spl.get('CONFIG_SPL_ROCKCHIP_ARMBIAN_ONCE_SD_REQUEST') == 'y'

    bl33 = open(os.path.join(fvdir, 'BL33_AP_UEFI.Fv'), 'rb').read()
    images = []
    for f in sorted(os.listdir(outdir)):
        mm = re.match(r'^' + re.escape(plat) + r'-([a-z]+)\.img$', f)
        if not mm:
            continue
        p = os.path.join(outdir, f)
        if not find_image_fv(p, bl33):
            die(f'{f} does not contain Build/{plat}/RELEASE_GCC/FV/BL33_AP_UEFI.Fv; package it again')
        images.append({'file': f, 'medium': mm.group(1), 'size': os.path.getsize(p), 'sha256': sha256(p)})
    if not images:
        die(f'no {plat}-<medium>.img in {outdir}; run scripts/package.sh {args.board} first')
    spl_in_image = None
    with open(os.path.join(outdir, images[0]['file']), 'rb') as f:
        spl_in_image = b'ArmbianOnce/1' in f.read()

    on_off = lambda v: 'on' if v else 'off'
    table_mode = pcd('PcdConfigTableModeDefault', 0)
    fan_modes = {0: 'always-on', 1: 'balanced', 2: 'performance', 3: 'quiet', 4: 'off'}
    request_on_sd = bool(pcd('PcdArmbianOnceRequestOnSd', 0))
    if spl:
        once_spl = spl_once and (spl_sd if request_on_sd else True)
    else:
        # A prebuilt SPL: the tag says it has the one-shot, not where it
        # looks for the request, so only an eMMC-request board can rely on it.
        once_spl = spl_in_image and not request_on_sd
    f = {
        'windows': {
            'built': has('AcpiTables') and has('RK3576AcpiPlatformDxe') and bool(table_mode & 1),
            'acpiTables': has('AcpiTables'),
            'configTableDefault': {1: 'acpi', 2: 'fdt', 3: 'acpi+fdt'}.get(table_mode, table_mode),
        },
        'armbianOnce': {
            'built': has('ArmbianOnce') and once_spl and spl_in_image,
            'uefiApp': has('ArmbianOnce'),
            'splBuilt': spl_once,
            'splTagInImage': spl_in_image,
            'requestOn': 'sd' if request_on_sd else 'emmc',
            'splSdRequest': spl_sd,
        },
        'hdmi': {'built': has('Vop2Dxe') and has('DwHdmiQpLib')},
        'dsi': {'built': has('Vop2Dxe') and has('DwMipiDsi2Lib')},
        'usbcDp': {'built': bool(pcd('PcdUsbcDpSupported', 0)) and has('RK3576Dxe'),
                   'default': on_off(pcd('PcdUsbcDpDefault', 0))},
        'cpuIdle': {'built': has('RK3576AcpiPlatformDxe'),
                    'default': on_off(pcd('PcdAcpiCpuIdleStatesDefault', 0))},
        'fanControl': {'built': bool(pcd('PcdAcpiFanControlSupported', 0)),
                       'default': fan_modes.get(pcd('PcdAcpiFanControlDefault', 0)),
                       'bootDutyPercent': pcd('PcdPwmFanSpeedDefault', 0)},
        'networkBoot': {'built': has('UefiPxeBcDxe'),
                        'default': on_off(pcd('PcdNetworkStackEnabledDefault', 0) and pcd('PcdNetworkStackPxeBootEnabledDefault', 0))},
        'httpBoot': {'built': has('HttpBootDxe'),
                     'default': on_off(pcd('PcdNetworkStackEnabledDefault', 0) and pcd('PcdNetworkStackHttpBootEnabledDefault', 0))},
        'secureBoot': {'built': has('SecureBootConfigDxe')},
    }
    for k, v in f.items():
        if not v['built']:
            v.pop('default', None)

    stores = []
    settings, omitted = hii_settings(os.path.join(build, 'AARCH64'), modules, pcds, stores)
    uncreated = check_setup_variables(root, stores)

    commit = subprocess.run(['git', '-C', root, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip() or None
    doc = {
        'schema': SCHEMA,
        'board': plat,
        'boardConfig': args.board,
        'commit': commit,
        'firmwareVersion': version,
        'bootMedium': conf.get('BOOT_MEDIUM', 'spi'),
        'images': images,
        'spl': {'built': bool(spl), 'armbianOnce': spl_once, 'armbianOnceSdRequest': spl_sd},
        'features': f,
        'settings': {'schema': SETTINGS_SCHEMA, 'options': settings, 'omitted': omitted},
    }

    expected = conf.get('FEATURES_EXPECTED', '').split()
    missing = [x for x in expected if x not in f or not f[x]['built']]
    unknown = [x for x in expected if x not in f]
    if unknown:
        die(f'FEATURES_EXPECTED in boards/{args.board}.conf names unknown features: {" ".join(unknown)}')

    out = args.out or os.path.join(outdir, 'features.json')
    with open(out, 'w') as fh:
        json.dump(doc, fh, indent=2, ensure_ascii=False)
        fh.write('\n')
    print(f'{out}: ' + ', '.join(k for k, v in f.items() if v['built']) + f'; {len(settings)} settings')
    if uncreated:
        die('setup variable stores no code creates (F10 in their form fails on a fresh variable store): '
            + ', '.join(uncreated))
    if missing:
        die(f'boards/{args.board}.conf expects {" ".join(missing)}, which the {plat} build does not have')


# ---------------------------------------------------------------- release

def cell(v):
    if not v['built']:
        return 'no'
    if v.get('default') == 'off':
        return 'yes, off by default'
    return 'yes'


def notes_table(doc):
    boards = list(doc['boards'])
    rows = ['| Feature | ' + ' | '.join(boards) + ' |', '|---|' + '---|' * len(boards)]
    for key, label in FEATURES:
        rows.append(f'| {label} | ' + ' | '.join(cell(doc['boards'][b]['features'][key]) for b in boards) + ' |')
    return '\n'.join(rows) + '\n'


def release(args):
    found = []
    for inp in args.inputs:
        for root, dirs, files in os.walk(inp):
            if 'features.json' in files:
                found.append(os.path.join(root, 'features.json'))
    if not found:
        die('no features.json under ' + ' '.join(args.inputs))
    covered = {os.path.dirname(x) for x in found}
    for inp in args.inputs:
        for root, dirs, files in os.walk(inp):
            imgs = [x for x in files if x.endswith('.img') and x != 'idbloader.img']
            if imgs and root not in covered:
                die(f'{root} has {" ".join(sorted(imgs))} but no features.json')
    os.makedirs(args.out, exist_ok=True)
    ver = args.asset_version or args.tag
    boards, commits = {}, set()
    for path in sorted(found):
        d = json.load(open(path))
        if d.get('schema') != SCHEMA:
            die(f'{path}: schema {d.get("schema")}, this script writes {SCHEMA}')
        b = d['board']
        if b in boards:
            die(f'two features.json for {b}')
        here = os.path.dirname(path)
        imgs = []
        for im in d['images']:
            src = os.path.join(here, im['file'])
            if not os.path.isfile(src):
                die(f'{b}: {im["file"]} is not next to its features.json')
            if sha256(src) != im['sha256']:
                die(f'{b}: {im["file"]} is not the image its features.json was made for')
            name = f'{b}-{im["medium"]}-edk2-{ver}.img'
            shutil.copyfile(src, os.path.join(args.out, name))
            imgs.append(dict(im, file=name))
        for x in sorted(os.listdir(here)):
            if x.endswith('.sfdisk'):
                shutil.copyfile(os.path.join(here, x), os.path.join(args.out, x))
        # Any other image next to it would go out without a features entry.
        stray = [x for x in os.listdir(here) if x.endswith('.img') and x not in {i['file'] for i in d['images']} and x != 'idbloader.img']
        if stray:
            die(f'{b}: images without a features.json entry: {" ".join(stray)}')
        commits.add(d.get('commit'))
        entry = {k: d[k] for k in ('firmwareVersion', 'bootMedium', 'spl', 'features', 'settings')}
        entry['images'] = imgs
        boards[b] = entry
    if len(commits) > 1 and not args.allow_mixed:
        die('the boards were built from different commits: ' + ' '.join(sorted(c or '?' for c in commits)))
    doc = {'schema': SCHEMA, 'tag': args.tag, 'assetVersion': ver,
           'commit': args.commit or (commits.pop() if len(commits) == 1 else None), 'boards': dict(sorted(boards.items()))}
    with open(os.path.join(args.out, 'FEATURES.json'), 'w') as fh:
        json.dump(doc, fh, indent=2, ensure_ascii=False)
        fh.write('\n')
    files = sorted(x for x in os.listdir(args.out) if x != 'SHA256SUMS.txt')
    with open(os.path.join(args.out, 'SHA256SUMS.txt'), 'w') as fh:
        for x in files:
            fh.write(f'{sha256(os.path.join(args.out, x))}  {x}\n')
    notes = args.notes or os.path.join(os.path.dirname(os.path.abspath(args.out)), 'features-notes.md')
    with open(notes, 'w') as fh:
        fh.write(notes_table(doc))
    print(open(notes).read())


def notes(args):
    sys.stdout.write(notes_table(json.load(open(args.file))))


def main():
    ap = argparse.ArgumentParser(description='What a firmware build supports, from the build output.')
    sub = ap.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('board')
    b.add_argument('board')
    b.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    b.add_argument('--out')
    r = sub.add_parser('release')
    r.add_argument('--tag', required=True)
    r.add_argument('--asset-version', help='the version part of the image names (default: the tag)')
    r.add_argument('--commit')
    r.add_argument('--out', required=True, help='directory for the release files')
    r.add_argument('--notes', help='where to write the notes table (default: features-notes.md next to --out)')
    r.add_argument('--allow-mixed', action='store_true', help='boards built from different commits are fine')
    r.add_argument('inputs', nargs='+')
    n = sub.add_parser('notes')
    n.add_argument('file')
    a = ap.parse_args()
    {'board': board, 'release': release, 'notes': notes}[a.cmd](a)


if __name__ == '__main__':
    main()

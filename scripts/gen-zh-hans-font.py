#!/usr/bin/env python3
#
# Build and check the Simplified Chinese (zh-Hans) glyph subset that
# Silicon/Rockchip/Drivers/ZhHansFontDxe installs into the HII database.
#
# EDK2's own HII font (GraphicsConsoleDxe's LaffStd) covers ASCII, Latin-1,
# box drawing and four arrows, nothing more.  A CJK string drawn with it comes
# out blank.  ZhHansFontDxe adds a simple-font package of wide (16x19) glyphs,
# but only for the characters our zh-Hans strings actually use, so the cost in
# the FV is a few KiB instead of a whole CJK font.
#
# The glyph list is derived from the strings, never maintained by hand:
#
#   - every .uni file in this repository (git ls-files), and
#   - the added lines of every patches/*.patch that touches a .uni file
#     (the TianoCore menus are translated through those patches).
#
# Usage:
#   gen-zh-hans-font.py check [--edk2 DIR]
#       Fail if a zh-Hans string uses a character that has no glyph, if a
#       wide character is not bracketed by \wide ... \narrow, or if a .uni
#       file that defines zh-Hans leaves a string untranslated.  With --edk2
#       (default third_party/edk2 when it exists) the patched TianoCore .uni
#       files are checked in full as well.  Needs no network and no font.
#
#   gen-zh-hans-font.py generate [--hex FILE]
#       Rewrite ZhHansGlyphs.c.  FILE is GNU Unifont's .hex(.gz) build; when
#       omitted it is downloaded from UNIFONT_URL and its SHA-256 checked.
#
#   gen-zh-hans-font.py wrap TEXT
#       Print TEXT with \wide and \narrow inserted, ready to paste into a
#       .uni string.  TEXT is .uni source text: \n, \\ and friends are kept.
#
# Why the markers matter: the setup browser (DisplayEngineDxe) and
# GraphicsConsoleDxe count screen columns from the \wide / \narrow control
# characters (U+FFF1 / U+FFF0) in the string, not from the glyphs.  A CJK
# character outside a \wide run is drawn 16 pixels wide but advances the
# cursor by 8, and the next character is drawn over its right half.
#
# Source font: GNU Unifont, dual-licensed SIL OFL 1.1 and GPL-2.0-or-later
# with the GNU font embedding exception (since Unifont 13.0.04).  Its CJK
# ideographs are the Wen Quan Yi glyphs, which follow mainland (G-source)
# shapes, so they are the right ones for zh-Hans.  The generated subset keeps
# that licence; see Silicon/Rockchip/Drivers/ZhHansFontDxe/OFL-1.1.txt.
#
# SPDX-License-Identifier: BSD-2-Clause-Patent
#

import argparse
import gzip
import hashlib
import os
import re
import subprocess
import sys
import tempfile
import unicodedata
import urllib.request

UNIFONT_VERSION = "18.0.01"
UNIFONT_URL = ("https://unifoundry.com/pub/unifont/unifont-%s/font-builds/"
               "unifont-%s.hex.gz" % (UNIFONT_VERSION, UNIFONT_VERSION))
UNIFONT_SHA256 = "e66385c79a0b8b24a466f3129930e08a966a935b4bf3b28c6bb17a9df9bf791d"
UNIFONT_COPYRIGHT = (
    "Copyright (C) 1998-2026 Roman Czyborra, Paul Hardy, Qianqian Fang, "
    "Andrew Miller, Johnnie Weaver, David Corbett, \u00c6lla Chiana Moskopp, "
    "Rebecca Bettencourt, Minseo Lee, Ho-Seok Ee, et al.")

LANG = "zh-Hans"

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GLYPH_FILE = os.path.join(ROOT, "Silicon", "Rockchip", "Drivers",
                          "ZhHansFontDxe", "ZhHansGlyphs.c")

# EFI_GLYPH_HEIGHT is 19 rows; Unifont glyphs are 16.  One blank row on top
# puts Unifont's baseline (under its row 13) on LaffStd's (under row 14), so
# CJK text sits on the same line as the Latin text around it.
EFI_GLYPH_HEIGHT = 19
TOP_PAD = 1

NARROW_CHAR = "\ufff0"
WIDE_CHAR = "\ufff1"

# Narrow glyphs GraphicsConsoleDxe's LaffStd font already has.  A narrow
# character outside this set would draw as nothing.
LAFF_NARROW = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100)) | {
    0x2190, 0x2191, 0x2192, 0x2193, 0x2500, 0x2502, 0x250C, 0x2510, 0x2514,
    0x2518, 0x251C, 0x2524, 0x252C, 0x2534, 0x253C, 0x2588, 0x2591, 0x25B2,
    0x25BA, 0x25BC, 0x25C4} | set(range(0x2550, 0x256D))


def is_wide(ch):
    return unicodedata.east_asian_width(ch) in ("W", "F")


# --------------------------------------------------------------------------
# .uni parsing.  This follows BaseTools' UniClassObject closely enough for the
# files we have: one #string per line, #language on the same line or on a
# continuation line, string text in one or more quoted pieces.
# --------------------------------------------------------------------------

BSLASH = "\x00BSLASH\x00"


def strip_comment(line):
    pos = line.find("//")
    while pos >= 0:
        if (line.count('"', 0, pos) - line.count('\\"', 0, pos)) & 1:
            pos = line.find("//", pos + 1)
        else:
            return line[:pos].strip()
    return line.strip()


def decode(text):
    """The escape handling of UniClassObject.PreProcess, in its order."""
    text = text.replace("\\\\", BSLASH)
    text = text.replace("\\wide", WIDE_CHAR).replace("\\narrow", NARROW_CHAR)
    text = text.replace("\\nbr", "\ufff2")
    text = text.replace("\\r\\n", "\r\n").replace("\\n", "\r\n")
    text = text.replace("\\r", "\r").replace("\\t", " ")
    text = text.replace('\\"', '"').replace("\\'", "'")
    return text.replace(BSLASH, "\\")


def quoted_pieces(line):
    """The quoted parts of one .uni line, still in source form."""
    return re.findall(r'"((?:[^"\\]|\\.)*)"', line)


def parse_uni(lines):
    """
    Return (langdefs, strings) where strings is a list of
    [name, lang, source_text, line_number].  name is None for a continuation
    #language line whose #string is not in `lines` (a patch hunk).
    """
    langdefs = {}
    strings = []
    name = None
    current = None
    for no, raw in enumerate(lines, 1):
        line = strip_comment(raw.rstrip("\r\n"))
        if not line:
            continue
        m = re.match(r"#langdef\s+(\S+)\s+\"(.*)\"", line)
        if m:
            langdefs[m.group(1)] = m.group(2)
            continue
        m = re.match(r"#string\s+(\S+)", line)
        if m:
            name = m.group(1)
            current = None
        parts = re.split(r"#language\s+", line)
        if len(parts) > 1:
            for part in parts[1:]:
                lang = part.split()[0]
                current = [name, lang, "".join(quoted_pieces(part)), no]
                strings.append(current)
        elif current is not None and line.startswith('"'):
            current[2] += "".join(quoted_pieces(line))
    return langdefs, strings


def width_errors(text):
    """Check \\wide / \\narrow placement in a decoded string."""
    errors = []
    wide = False
    for ch in text:
        if ch == WIDE_CHAR:
            wide = True
        elif ch == NARROW_CHAR:
            wide = False
        elif ord(ch) < 0x20 or ch == "\ufff2":
            continue
        elif is_wide(ch):
            if not wide:
                errors.append("U+%04X %s is outside \\wide" % (ord(ch), ch))
        else:
            if wide:
                errors.append("U+%04X %r is inside \\wide" % (ord(ch), ch))
            if ord(ch) not in LAFF_NARROW:
                errors.append("U+%04X %r has no narrow glyph" % (ord(ch), ch))
    if wide:
        errors.append("string ends inside \\wide (add \\narrow)")
    return errors


# --------------------------------------------------------------------------
# Collecting the strings
# --------------------------------------------------------------------------

def repo_uni_files():
    out = subprocess.run(["git", "-C", ROOT, "ls-files", "*.uni"],
                         check=True, capture_output=True, text=True).stdout
    return [os.path.join(ROOT, p) for p in out.split()
            if not p.startswith("third_party/")]


def patch_uni_hunks():
    """{patch-relative .uni path: [added lines]} over all patches/*.patch."""
    hunks = {}
    pdir = os.path.join(ROOT, "patches")
    for name in sorted(os.listdir(pdir)):
        if not name.endswith(".patch"):
            continue
        target = None
        with open(os.path.join(pdir, name), encoding="utf-8", newline="") as f:
            for line in f:
                if line.startswith("+++ "):
                    path = line[4:].strip()
                    path = path[2:] if path.startswith("b/") else path
                    target = path if path.lower().endswith(".uni") else None
                elif target and line.startswith("+"):
                    hunks.setdefault(target, []).append(line[1:])
    return hunks


def read_lines(path):
    with open(path, encoding="utf-8-sig", newline="") as f:
        return f.read().splitlines()


def collect(edk2):
    """
    Return (sources, problems): sources is a list of (label, name, decoded
    zh-Hans text); problems a list of error strings.
    """
    sources = []
    problems = []

    def check_file(label, lines, full):
        langdefs, strings = parse_uni(lines)
        # BaseTools' UNI parser knows \n, \r, \t, \", \', \\, \wide,
        # \narrow, \nbr and \xHHHH\, but not \uHHHH, which it leaves in the
        # string as six characters.  Write the character itself instead.
        for name, lang, text, no in strings:
            if re.search(r"\\u[0-9A-Fa-f]{4}", text.replace("\\\\", "")):
                problems.append("%s:%d %s (%s): \\u escapes are not "
                                "understood by the UNI compiler and show "
                                "literally" % (label, no, name or "?", lang))
        has_lang = LANG in langdefs or any(s[1] == LANG for s in strings)
        if not has_lang:
            return
        if full and LANG not in langdefs:
            problems.append("%s: zh-Hans strings but no #langdef zh-Hans" % label)
        names = {s[0] for s in strings if s[0] is not None}
        done = {s[0] for s in strings if s[1] == LANG}
        if full:
            for missing in sorted(names - done):
                problems.append("%s: %s has no zh-Hans text (the setup "
                                "browser would draw it as \"!\", not fall "
                                "back to English)" % (label, missing))
        for name, lang, text, no in strings:
            if lang != LANG:
                continue
            decoded = decode(text)
            for err in width_errors(decoded):
                problems.append("%s:%d %s: %s" % (label, no, name or "?", err))
            sources.append((label, name, decoded))
        if LANG in langdefs:
            decoded = decode(langdefs[LANG])
            for err in width_errors(decoded):
                problems.append("%s: #langdef zh-Hans: %s" % (label, err))
            sources.append((label, "#langdef", decoded))

    for path in repo_uni_files():
        check_file(os.path.relpath(path, ROOT), read_lines(path), True)

    for target, added in sorted(patch_uni_hunks().items()):
        full_path = os.path.join(edk2, target) if edk2 else None
        if full_path and os.path.isfile(full_path):
            check_file("edk2/" + target, read_lines(full_path), True)
        else:
            check_file("patch:" + target, [l.rstrip("\r\n") for l in added],
                       False)
    return sources, problems


def wide_chars(sources):
    chars = set()
    for _, _, text in sources:
        chars.update(ch for ch in text if is_wide(ch))
    return sorted(chars)


# --------------------------------------------------------------------------
# Glyph file
# --------------------------------------------------------------------------

def glyphs_in_file():
    if not os.path.isfile(GLYPH_FILE):
        return set()
    with open(GLYPH_FILE, encoding="utf-8") as f:
        return {int(m, 16) for m in re.findall(r"^\s*\{\s*0x([0-9A-Fa-f]{4}),",
                                                f.read(), re.M)}


def load_unifont(path):
    if path is None:
        tmp = tempfile.mkdtemp(prefix="unifont-")
        path = os.path.join(tmp, os.path.basename(UNIFONT_URL))
        print("downloading %s" % UNIFONT_URL, file=sys.stderr)
        urllib.request.urlretrieve(UNIFONT_URL, path)
        with open(path, "rb") as f:
            digest = hashlib.sha256(f.read()).hexdigest()
        if digest != UNIFONT_SHA256:
            sys.exit("error: %s has SHA-256 %s, expected %s"
                     % (path, digest, UNIFONT_SHA256))
    opener = gzip.open if path.endswith(".gz") else open
    font = {}
    with opener(path, "rt", encoding="ascii") as f:
        for line in f:
            cp, _, bits = line.strip().partition(":")
            if cp and bits:
                font[int(cp, 16)] = bits
    return font


def to_efi_wide(bits):
    """Unifont hex (16 rows, 8 or 16 px) -> (GlyphCol1, GlyphCol2)."""
    if len(bits) == 64:
        rows = [int(bits[i:i + 4], 16) for i in range(0, 64, 4)]
    elif len(bits) == 32:
        # An 8 px glyph for a wide code point: centre it in the 16 px cell.
        rows = [int(bits[i:i + 2], 16) << 4 for i in range(0, 32, 2)]
    else:
        raise ValueError("unexpected glyph length %d" % len(bits))
    rows = [0] * TOP_PAD + rows
    rows += [0] * (EFI_GLYPH_HEIGHT - len(rows))
    return [r >> 8 for r in rows], [r & 0xFF for r in rows]


def write_glyph_file(chars, font):
    missing = [c for c in chars if ord(c) not in font]
    if missing:
        sys.exit("error: Unifont has no glyph for %s"
                 % " ".join("U+%04X" % ord(c) for c in missing))

    def hexlist(values):
        return ", ".join("0x%02X" % v for v in values)

    out = []
    out.append("/** @file\n")
    out.append("  Wide (16x19) glyphs for the zh-Hans setup strings.\n\n")
    out.append("  GENERATED by scripts/gen-zh-hans-font.py from GNU Unifont "
               "%s; do not\n  edit.  Re-run the script after changing a "
               "zh-Hans string.\n\n" % UNIFONT_VERSION)
    out.append("  The glyph bitmaps are a subset of GNU Unifont:\n")
    words = UNIFONT_COPYRIGHT.split()
    line = " "
    for w in words:
        if len(line) + len(w) + 1 > 78:
            out.append(line + "\n")
            line = " "
        line += " " + w
    out.append(line + "\n\n")
    out.append("  SPDX-License-Identifier: OFL-1.1 OR GPL-2.0-or-later WITH "
               "Font-exception-2.0\n")
    out.append("**/\n\n")
    out.append("#include \"ZhHansFontDxe.h\"\n\n")
    out.append("CONST EFI_WIDE_GLYPH  gZhHansWideGlyphs[] = {\n")
    for c in chars:
        col1, col2 = to_efi_wide(font[ord(c)])
        out.append("  // U+%04X %s\n" % (ord(c), c))
        out.append("  { 0x%04X, 0x00,\n" % ord(c))
        out.append("    { %s },\n" % hexlist(col1))
        out.append("    { %s },\n" % hexlist(col2))
        out.append("    { 0x00, 0x00, 0x00 } },\n")
    out.append("};\n\n")
    out.append("CONST UINTN  gZhHansWideGlyphCount = ARRAY_SIZE (gZhHansWideGlyphs);\n")
    with open(GLYPH_FILE, "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(out))


# --------------------------------------------------------------------------
# wrap
# --------------------------------------------------------------------------

def wrap(text):
    """Insert \\wide / \\narrow into .uni source text."""
    tokens = re.findall(r"\\(?:wide|narrow|nbr|r\\n|[nrt\"'\\])|.", text, re.S)
    out = []
    wide = False
    for tok in tokens:
        if tok in ("\\wide", "\\narrow"):
            continue
        # A line break closes a \wide run, so every line of a help text can
        # be read (and edited) on its own.
        tok_wide = len(tok) == 1 and is_wide(tok)
        if tok_wide and not wide:
            out.append("\\wide")
            wide = True
        elif not tok_wide and wide:
            out.append("\\narrow")
            wide = False
        out.append(tok)
    if wide:
        out.append("\\narrow")
    return "".join(out)


# --------------------------------------------------------------------------

def default_edk2():
    path = os.path.join(ROOT, "third_party", "edk2")
    return path if os.path.isdir(os.path.join(path, "MdeModulePkg")) else None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("--edk2", default=default_edk2())
    g = sub.add_parser("generate")
    g.add_argument("--hex")
    g.add_argument("--edk2", default=default_edk2())
    w = sub.add_parser("wrap")
    w.add_argument("text")
    args = ap.parse_args()

    if args.cmd == "wrap":
        print(wrap(args.text))
        return 0

    sources, problems = collect(args.edk2)
    chars = wide_chars(sources)

    if args.cmd == "generate":
        if problems:
            print("\n".join(problems), file=sys.stderr)
            return 1
        write_glyph_file(chars, load_unifont(args.hex))
        print("%s: %d glyphs (%d bytes of EFI_WIDE_GLYPH)"
              % (os.path.relpath(GLYPH_FILE, ROOT), len(chars), len(chars) * 44))
        return 0

    have = glyphs_in_file()
    for c in chars:
        if ord(c) not in have:
            problems.append("U+%04X %s is used but has no glyph in %s (run "
                            "generate)" % (ord(c), c,
                                           os.path.relpath(GLYPH_FILE, ROOT)))
    unused = have - {ord(c) for c in chars}
    if unused:
        problems.append("%d glyphs in %s are no longer used (run generate): %s"
                        % (len(unused), os.path.relpath(GLYPH_FILE, ROOT),
                           " ".join("U+%04X" % u for u in sorted(unused))))
    if problems:
        print("\n".join(problems), file=sys.stderr)
        print("zh-Hans check: %d problem(s)" % len(problems), file=sys.stderr)
        return 1
    print("zh-Hans check: %d strings, %d distinct wide characters, all drawn"
          % (len(sources), len(chars)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

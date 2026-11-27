// SPDX-License-Identifier: BSD-2-Clause-Patent
/*
 * Silicon/Rockchip/Library/PlatformBootManagerLib/WindowsBcd.h on the host.
 *
 * The hives are built here, cell by cell, in the layout of a store written
 * by bcdboot: a root key, Objects, {bootmgr} with Elements\23000003 naming
 * the default entry, and that entry with Elements\12000004.  Checked:
 *
 *  - the description comes back ("Windows 11"), through lh, lf, li and ri
 *    subkey lists, compressed and UTF-16 key names, any case;
 *  - a missing or malformed default GUID, a missing entry, a REG_DWORD where
 *    a string should be, control characters, an over-long name cut to fit;
 *  - every truncation of the file and 200000 random corruptions, each in a
 *    buffer of exactly the file's size so that AddressSanitizer sees any
 *    read past the end.
 *
 * Arguments are real BCD files: what is read from each is printed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../Silicon/Rockchip/Library/PlatformBootManagerLib/WindowsBcd.h"

static int fails, checks;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            fails++;                                                           \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                        \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

/* ---- a tiny hive writer ---- */

static unsigned char hive[65536];
static unsigned used;   /* bytes of hive bins in use, after the base block */

static void put16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void put32(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

/* A new allocated cell with Len data bytes; returns its offset. */
static unsigned cell(unsigned len)
{
    unsigned size = (len + 4 + 7) & ~7u;
    unsigned off = used;
    if (WBCD_BASE_BLOCK_SIZE + off + size > sizeof hive) {
        fprintf(stderr, "hive full\n");
        exit(2);
    }
    put32(hive + WBCD_BASE_BLOCK_SIZE + off, 0u - size);
    used += size;
    return off;
}

static unsigned char *at(unsigned off) { return hive + WBCD_BASE_BLOCK_SIZE + off + 4; }

static int utf16_names;   /* write key names as UTF-16 instead of Latin-1 */

static unsigned key(const char *name)
{
    unsigned n = strlen(name), len = utf16_names ? 2 * n : n, off = cell(0x4C + len), i;
    unsigned char *nk = at(off);
    nk[0] = 'n';
    nk[1] = 'k';
    put16(nk + 2, utf16_names ? 0 : WBCD_KEY_COMP_NAME);
    put16(nk + 0x48, len);
    for (i = 0; i < n; i++) {
        if (utf16_names)
            put16(nk + 0x4C + 2 * i, (unsigned char)name[i]);
        else
            nk[0x4C + i] = name[i];
    }
    return off;
}

/* Subkey list of the given kind ("lh", "lf", "li", or "ri" wrapping an li per child). */
static void subkeys(unsigned parent, const char *kind, const unsigned *child, unsigned n)
{
    unsigned stride = (kind[0] == 'l' && (kind[1] == 'h' || kind[1] == 'f')) ? 8 : 4;
    unsigned off = cell(4 + stride * n), i;
    unsigned char *l = at(off);
    l[0] = kind[0];
    l[1] = kind[1];
    put16(l + 2, n);
    for (i = 0; i < n; i++) {
        unsigned target = child[i];
        if (kind[0] == 'r') {
            unsigned li = cell(8);
            at(li)[0] = 'l';
            at(li)[1] = 'i';
            put16(at(li) + 2, 1);
            put32(at(li) + 4, child[i]);
            target = li;
        }
        put32(l + 4 + stride * i, target);
    }
    put32(at(parent) + 0x14, n);
    put32(at(parent) + 0x1C, off);
}

/* One value on a key; Data of Bytes bytes, inline when it fits in 4. */
static void value(unsigned k, const char *name, unsigned type, const void *data, unsigned bytes)
{
    unsigned n = strlen(name), vk = cell(0x14 + n), list = cell(4);
    unsigned char *v = at(vk);
    v[0] = 'v';
    v[1] = 'k';
    put16(v + 2, n);
    put32(v + 0x0C, type);
    put16(v + 0x10, WBCD_VALUE_COMP_NAME);
    memcpy(v + 0x14, name, n);
    if (bytes <= 4) {
        put32(v + 4, bytes | 0x80000000u);
        memcpy(v + 8, data, bytes);
    } else {
        unsigned d = cell(bytes);
        put32(v + 4, bytes);
        put32(v + 8, d);
        memcpy(at(d), data, bytes);
    }
    put32(at(list), vk);
    put32(at(k) + 0x24, 1);
    put32(at(k) + 0x28, list);
}

static void string_value(unsigned k, const char *ascii)
{
    unsigned short w[200];
    unsigned n = strlen(ascii), i;
    for (i = 0; i < n; i++)
        w[i] = (unsigned char)ascii[i];
    w[n] = 0;
    value(k, "Element", WBCD_REG_SZ, w, 2 * (n + 1));
}

/* Objects\<object>\Elements\<element> = Text; returns nothing, links Objects. */
static unsigned object(const char *guid, const char *element, const char *text, const char *kind)
{
    unsigned o = key(guid), els = key("Elements"), el = key(element);
    string_value(el, text);
    subkeys(els, kind, &el, 1);
    subkeys(o, kind, &els, 1);
    return o;
}

/*
 * A store: root, Objects with {bootmgr} (default = Default) and the entry
 * Guid with description Name, plus a third object first in the list.
 */
static unsigned build(const char *kind, const char *def, const char *guid, const char *name)
{
    unsigned root, objs, list[3];
    memset(hive, 0, sizeof hive);
    used = 0;
    memcpy(hive, "regf", 4);
    put32(hive + 0x14, 1);
    put32(hive + 0x18, 5);
    root = key("NewStoreRoot");
    put32(hive + 0x24, root);
    objs = key("Objects");
    list[0] = object("{0ce4991b-e6b3-4b16-b23c-5e0d9250e5d9}", "16000020", "x", kind);
    list[1] = object(WBCD_BOOTMGR_GUID, "23000003", def, kind);
    list[2] = guid ? object(guid, "12000004", name, kind) : object("{1afa9c49-16ab-4a5c-901b-212802da9460}", "11000001", "y", kind);
    subkeys(objs, kind, list, 3);
    subkeys(root, kind, &objs, 1);
    put32(hive + 0x28, (used + 4095) & ~4095u);
    return WBCD_BASE_BLOCK_SIZE + ((used + 4095) & ~4095u);
}

static int read_name(const unsigned char *file, unsigned size, char *ascii, unsigned out_chars)
{
    wbcd_u16 out[80];
    unsigned char *copy = malloc(size ? size : 1);
    int r, i;
    memcpy(copy, file, size);
    r = WbcdDefaultDescription(copy, size, out, out_chars);
    free(copy);
    for (i = 0; out[i] && i < 79; i++)
        ascii[i] = out[i] < 0x80 ? (char)out[i] : '?';
    ascii[i] = 0;
    return r;
}

#define G "{2b693006-ca28-11f1-a7e0-a4e441d70d63}"

int main(int argc, char **argv)
{
    static const char *kinds[] = { "lh", "lf", "li", "ri" };
    char name[80];
    unsigned size, k, i, s;
    int r;

    for (k = 0; k < 8; k++) {
        utf16_names = k >= 4;
        size = build(kinds[k % 4], G, "{2B693006-CA28-11F1-A7E0-A4E441D70D63}", "Windows 11");
        r = read_name(hive, size, name, 80);
        CHECK(r == WBCD_OK && !strcmp(name, "Windows 11"), "%s%s: %d \"%s\"", kinds[k % 4], utf16_names ? " utf16" : "", r, name);
    }
    utf16_names = 0;

    size = build("lh", G, G, "Windows 10  ");
    r = read_name(hive, size, name, 80);
    CHECK(r == WBCD_OK && !strcmp(name, "Windows 10"), "trailing spaces trimmed: \"%s\"", name);

    size = build("lh", G, G, "Windows 11 Enterprise LTSC");
    r = read_name(hive, size, name, 8);
    CHECK(r == WBCD_OK && !strcmp(name, "Windows"), "cut to 7 characters: \"%s\"", name);

    size = build("lh", G, NULL, NULL);
    CHECK(read_name(hive, size, name, 80) == WBCD_E_NO_ENTRY && name[0] == 0, "default entry missing");

    size = build("lh", "{2b693006-ca28-11f1-a7e0-a4e441d70d6}", G, "Windows 11");
    CHECK(read_name(hive, size, name, 80) == WBCD_E_DEFAULT, "short GUID");

    size = build("lh", "{2b693006-ca28-11f1-a7e0-a4e441d70d63}x", G, "Windows 11");
    CHECK(read_name(hive, size, name, 80) == WBCD_E_DEFAULT, "GUID with a tail");

    size = build("lh", "{2b693006Xca28-11f1-a7e0-a4e441d70d63}", G, "Windows 11");
    CHECK(read_name(hive, size, name, 80) == WBCD_E_DEFAULT, "GUID without a dash");

    size = build("lh", G, G, "Win\x01dows");
    CHECK(read_name(hive, size, name, 80) == WBCD_E_NAME && name[0] == 0, "control character");

    size = build("lh", G, G, "   ");
    CHECK(read_name(hive, size, name, 80) == WBCD_E_NAME, "only spaces");

    size = build("lh", G, G, "Windows 11");
    memcpy(hive, "regg", 4);
    CHECK(read_name(hive, size, name, 80) == WBCD_E_HEADER, "not regf");

    /* the 23000003 value as a REG_DWORD */
    size = build("lh", G, G, "Windows 11");
    {
        unsigned off;
        for (off = 0; off + 8 < used; off += 8) {
            unsigned char *c = hive + WBCD_BASE_BLOCK_SIZE + off + 4;
            if (c[0] == 'v' && c[1] == 'k')
                put32(c + 0x0C, 4);
        }
    }
    CHECK(read_name(hive, size, name, 80) == WBCD_E_NO_BOOTMGR, "REG_DWORD instead of REG_SZ");

    /* every truncation of a good store */
    size = build("ri", G, G, "Windows 11");
    for (s = 0; s <= size; s++)
        read_name(hive, s, name, 80);
    CHECK(read_name(hive, size, name, 80) == WBCD_OK, "full size after the truncation run");

    /* random corruption: 1 to 8 bytes, anywhere in the used part */
    {
        static unsigned char good[sizeof hive];
        unsigned seed = 12345, ok = 0, n;
        unsigned span = WBCD_BASE_BLOCK_SIZE + used;
        memcpy(good, hive, size);
        for (i = 0; i < 200000; i++) {
            memcpy(hive, good, size);
            for (n = 0; n <= (seed >> 28) % 8; n++) {
                seed = seed * 1103515245u + 12345u;
                unsigned pos = (seed >> 8) % span;
                seed = seed * 1103515245u + 12345u;
                hive[pos < 0x30 ? pos : WBCD_BASE_BLOCK_SIZE + (pos % used)] = seed >> 16;
            }
            if (read_name(hive, size, name, 80) == WBCD_OK)
                ok++;
        }
        printf("corruption run: %u of 200000 still read a name\n", ok);
    }

    for (i = 1; i < (unsigned)argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        static unsigned char buf[4 << 20];
        size_t n;
        if (!f) {
            perror(argv[i]);
            fails++;
            continue;
        }
        n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        r = read_name(buf, (unsigned)n, name, 80);
        printf("%s: %d \"%s\"\n", argv[i], r, name);
    }

    printf("%d of %d checks failed\n", fails, checks);
    return fails != 0;
}

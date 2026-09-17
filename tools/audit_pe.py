#!/usr/bin/env python3
"""Strict Windows-loader-conformance audit for FPS Booster executables.

Checks everything the Windows 10/11 loader and shell care about:
headers, sections, imports, subsystem, checksum, manifest XML,
version block, icon group, and a strictly 3-level sorted resource tree.

Usage: audit_pe.py FILE.exe --ver 1.5.0
Exit code 0 = all checks passed.
"""
import struct
import sys
import xml.etree.ElementTree as ET

FAILS = []


def check(name, ok, extra=''):
    print('%s %-28s %s' % ('PASS' if ok else 'FAIL', name, extra))
    if not ok:
        FAILS.append(name)


def u16(d, o):
    return struct.unpack('<H', d[o:o + 2])[0]


def u32(d, o):
    return struct.unpack('<I', d[o:o + 4])[0]


def pe_checksum(d, co):
    """Official PE checksum algorithm (sum with carry, skip checksum field)."""
    s = 0
    n = len(d)
    for i in range(0, n - 1, 2):
        if i == co or i == co + 2:  # checksum field is a DWORD: skip both words
            continue
        s += d[i] | (d[i + 1] << 8)
        s = (s & 0xFFFFFFFF) + (s >> 32)
    if n % 2:
        s += d[n - 1]
        s = (s & 0xFFFFFFFF) + (s >> 32)
    s = (s & 0xFFFF) + (s >> 16)
    s = s + (s >> 16)
    return (s & 0xFFFF) + n


def main():
    if len(sys.argv) < 2:
        sys.exit('usage: audit_pe.py FILE.exe [--ver 1.5.0]')
    path = sys.argv[1]
    ver = '1.5.0'
    if '--ver' in sys.argv:
        ver = sys.argv[sys.argv.index('--ver') + 1]
    d = open(path, 'rb').read()

    # ---- DOS + COFF ----
    check('MZ signature', d[0:2] == b'MZ')
    pe = u32(d, 0x3C)
    check('PE signature', d[pe:pe + 4] == b'PE\0\0')
    coff = pe + 4
    machine = u16(d, coff)
    nsec = u16(d, coff + 2)
    optsz = u16(d, coff + 16)
    chars = u16(d, coff + 18)
    check('machine x64', machine == 0x8664, hex(machine))
    check('executable, not dll', bool(chars & 0x2) and not (chars & 0x2000), hex(chars))
    opt = coff + 20
    check('PE32+ magic', u16(d, opt) == 0x20B)
    check('rva/sizes == 16', u32(d, opt + 108) == 16)
    sec_align = u32(d, opt + 32)
    file_align = u32(d, opt + 36)
    check('alignments', sec_align == 0x1000 and file_align == 0x200,
          'sec=%s file=%s' % (hex(sec_align), hex(file_align)))
    img_size = u32(d, opt + 56)
    hdr_size = u32(d, opt + 60)
    check('subsystem GUI', u16(d, opt + 68) == 2)
    entry = u32(d, opt + 16)

    # ---- sections ----
    st = opt + optsz
    secs = []
    for i in range(nsec):
        o = st + i * 40
        name = d[o:o + 8].rstrip(b'\0')
        vsz, vaddr, rawsz, rawptr = struct.unpack('<IIII', d[o + 8:o + 24])
        flags = u32(d, o + 36)
        secs.append(dict(name=name, vsz=vsz, vaddr=vaddr, rawsz=rawsz, rawptr=rawptr, flags=flags))
    check('section table in headers', st + nsec * 40 <= hdr_size)
    ok = True
    for s in secs:
        if s['rawptr'] + s['rawsz'] > len(d):
            ok = False
        if s['vaddr'] % sec_align != 0:
            ok = False
    check('sections mapped + aligned', ok)
    expect_img = 0
    for s in secs:
        end = s['vaddr'] + ((max(s['vsz'], 1) + sec_align - 1) // sec_align) * sec_align
        expect_img = max(expect_img, end)
    check('SizeOfImage exact', img_size == expect_img, '%s==%s' % (hex(img_size), hex(expect_img)))
    in_exec = any(s['vaddr'] <= entry < s['vaddr'] + s['vsz'] and (s['flags'] & 0x20000000) for s in secs)
    check('entry in exec section', in_exec, hex(entry))

    def rva2off(rva):
        for s in secs:
            if s['vaddr'] <= rva < s['vaddr'] + max(s['vsz'], s['rawsz']):
                return s['rawptr'] + (rva - s['vaddr'])
        return None

    # ---- imports ----
    irva, isz = struct.unpack('<II', d[opt + 120:opt + 128])
    check('import dir present', irva != 0 and isz != 0)
    dlls = []
    off = rva2off(irva)
    if off is not None:
        while True:
            ilt, ts, fw, nm, iat = struct.unpack('<IIIII', d[off:off + 20])
            if ilt == ts == fw == nm == iat == 0:
                break
            noff = rva2off(nm)
            end = d.index(b'\0', noff)
            dlls.append(d[noff:end].decode('ascii'))
            off += 20
            if len(dlls) > 100:
                break
    up = [x.upper() for x in dlls]
    check('core imports', 'KERNEL32.DLL' in up and 'USER32.DLL' in up, '%d dlls' % len(dlls))

    # ---- checksum ----
    co = opt + 64
    stored = u32(d, co)
    calc = pe_checksum(d, co)
    check('checksum valid', stored != 0 and stored == calc, '%s==%s' % (hex(stored), hex(calc)))

    # ---- resources: strict 3-level sorted tree ----
    rrva, rsz = struct.unpack('<II', d[opt + 128:opt + 136])
    check('resource dir present', rrva != 0 and rsz != 0)
    base = rva2off(rrva)

    def rdir(o):
        nn, nid = struct.unpack('<HH', d[base + o + 12:base + o + 16])
        return [(u32(d, base + o + 16 + i * 8), u32(d, base + o + 20 + i * 8))
                for i in range(nn + nid)], nn, nid

    def rstr(o):
        ln = u16(d, base + o)
        return d[base + o + 2:base + o + 2 + ln * 2].decode('utf-16-le')

    found = {}
    order_ok = True
    three_ok = True
    if base is not None:
        tents, _, _ = rdir(0)
        # root order: named first (alpha), then IDs (numeric)
        keys = [('n' if a & 0x80000000 else 'i',
                 rstr(a & 0x7FFFFFFF) if a & 0x80000000 else (a & 0xFFFF)) for a, b in tents]
        if keys != sorted([k for k in keys if k[0] == 'n']) + sorted([k for k in keys if k[0] == 'i']):
            order_ok = False
        for ta, tb in tents:
            t = rstr(ta & 0x7FFFFFFF) if ta & 0x80000000 else (ta & 0xFFFF)
            if not (tb & 0x80000000):
                three_ok = False
                continue
            nents, _, _ = rdir(tb & 0x7FFFFFFF)
            ids = [a & 0xFFFF for a, b in nents if not (a & 0x80000000)]
            if ids != sorted(ids):
                order_ok = False
            for na, nb in nents:
                n = rstr(na & 0x7FFFFFFF) if na & 0x80000000 else (na & 0xFFFF)
                if not (nb & 0x80000000):
                    three_ok = False
                    continue
                lents, _, _ = rdir(nb & 0x7FFFFFFF)
                for la, lb in lents:
                    if lb & 0x80000000:
                        three_ok = False  # must point at DATA, not a 4th dir
                        continue
                    do = base + (lb & 0x7FFFFFFF)
                    drva, dsz = struct.unpack('<II', d[do:do + 8])
                    doff = rva2off(drva)
                    if doff is None:
                        three_ok = False
                        continue
                    found[(t, n, la & 0xFFFF)] = d[doff:doff + dsz]
    check('resource tree 3-level', three_ok, '%d items' % len(found))
    check('resource order sorted', order_ok)

    def blob(t, n):
        for (tt, nn, lang), b in found.items():
            if tt == t and nn == n:
                return b
        return None

    # ---- icon ----
    grp = blob(14, 101)
    gok = False
    if grp and len(grp) >= 6:
        _, typ, cnt = struct.unpack('<HHH', grp[:6])
        gok = (typ == 1 and cnt > 0 and len(grp) == 6 + cnt * 14)
        if gok:
            for i in range(cnt):
                eid = struct.unpack('<H', grp[6 + i * 14 + 12:6 + i * 14 + 14])[0]
                ib = blob(3, eid)
                if not ib or len(ib) < 40:
                    gok = False
                    break
                if ib[:8] == b'\x89PNG\r\n\x1a\n':
                    continue
                w, h = struct.unpack('<II', ib[4:12])[0], struct.unpack('<I', ib[8:12])[0]
                if ib[0:4] != struct.pack('<I', 40) or w == 0 or h == 0:
                    gok = False
                    break
    check('icon group 101 valid', gok)

    # ---- manifest ----
    mani = blob(24, 1)
    mok = False
    if mani:
        try:
            root = ET.fromstring(mani.decode('utf-8'))
            txt = mani.decode('utf-8')
            mok = 'requireAdministrator' in txt and 'assemblyIdentity' in txt
        except Exception:
            mok = False
    check('manifest XML + admin', mok)

    # ---- version ----
    ver_b = blob(16, 1)
    vok = False
    if ver_b and len(ver_b) >= 52:
        wlen = u16(ver_b, 0)
        sig = u32(ver_b, 40) if len(ver_b) > 60 else 0
        parts = (ver.split('.') + ['0'] * 4)[:4]
        nums = [int(x) for x in parts]
        ms = (nums[0] << 16) | nums[1]
        ls = (nums[2] << 16) | nums[3]
        ffi_off = (6 + 15 * 2 + 2 + 3) // 4 * 4  # 'VS_VERSION_INFO\0' = 15 wchars = 30 bytes
        if len(ver_b) >= ffi_off + 52:
            vals = struct.unpack('<13I', ver_b[ffi_off:ffi_off + 52])
            vok = (wlen == len(ver_b) and vals[0] == 0xFEEF04BD and
                   vals[2] == ms and vals[3] == ls and sig == vals[0])
    check('version %s valid' % ver, vok)

    # ---- PNG + DB ----
    for rid, nm in ((201, 'logo'), (202, 'banner'), (203, 'bg')):
        b = blob('PNG', rid)
        check('png %s (%d)' % (nm, rid), b is not None and b[:8] == b'\x89PNG\r\n\x1a\n')
    db = blob(10, 301)
    check('games db (301)', db is not None and len(db) > 100)

    print()
    if FAILS:
        print('AUDIT FAILED: %d check(s)' % len(FAILS))
        return 1
    print('AUDIT PASSED: all checks green.')
    return 0


if __name__ == '__main__':
    sys.exit(main())

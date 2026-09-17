#!/usr/bin/env python3
"""Inject a .rsrc section into a PE32+ file, set GUI subsystem, and validate.

Resources: app icon, UAC manifest, version info, UI images, game-tips DB.
Usage:
  inject_resources.py RAW.exe OUT.exe --icon icon.ico --manifest app.manifest
      --logo logo_ui.png --banner banner_ui.png --db games_db.json
"""
import struct
import sys
import os
import argparse

RT_ICON = 3
RT_RCDATA = 10
RT_GROUP_ICON = 14
RT_VERSION = 16
RT_MANIFEST = 24
LANG = 1033


def align_up(v, a):
    return (v + a - 1) // a * a


# ---------------- ICO ----------------
def parse_ico(path):
    d = open(path, 'rb').read()
    reserved, typ, count = struct.unpack('<HHH', d[:6])
    assert reserved == 0 and typ == 1 and count > 0, 'bad ico'
    imgs = []
    for i in range(count):
        w, h, cc, res, planes, bitc, sz, off = struct.unpack('<BBBBHHII', d[6 + i * 16:6 + i * 16 + 16])
        imgs.append({'w': w or 256, 'h': h or 256, 'planes': planes,
                     'bitc': bitc, 'data': d[off:off + sz]})
    return imgs


def group_icon(imgs, base_id=1):
    out = struct.pack('<HHH', 0, 1, len(imgs))
    for i, im in enumerate(imgs):
        out += struct.pack('<BBBBHHIH',
                           0 if im['w'] == 256 else im['w'],
                           0 if im['h'] == 256 else im['h'],
                           0, 0, im['planes'], im['bitc'],
                           len(im['data']), base_id + i)
    return out


# ---------------- Version info ----------------
def _pad4(b):
    return b + b'\0' * ((4 - len(b) % 4) % 4)


def _block(key, vtype, value, children=b'', vlen_override=None):
    head = struct.pack('<HHH', 0, 0, vtype) + key.encode('utf-16-le') + b'\0\0'
    head = _pad4(head)
    body = head + value
    if children:
        body = _pad4(body) + children
    total = _pad4(body)
    if vlen_override is not None:
        vlen = vlen_override
    elif value and not children:
        vlen = len(value) // 2 if vtype == 1 else len(value)
    else:
        vlen = 0
    return struct.pack('<HHH', len(total), vlen, vtype) + total[6:]


def version_info(ver='1.0.0'):
    parts = (ver.split('.') + ['0'] * 4)[:4]
    nums = [int(x) for x in parts]
    ms = (nums[0] << 16) | nums[1]
    ls = (nums[2] << 16) | nums[3]
    ffi = struct.pack('<13I', 0xFEEF04BD, 0x10000, ms, ls, ms, ls,
                      0x3F, 0, 0x40004, 1, 0, 0, 0)
    strings = [
        ('CompanyName', 'FPS Booster'),
        ('FileDescription', 'FPS Booster Pro - Ultimate Game FPS Optimizer'),
        ('FileVersion', ver),
        ('InternalName', 'fpsbooster'),
        ('OriginalFilename', 'fpsbooster.exe'),
        ('ProductName', 'FPS Booster Pro'),
        ('ProductVersion', ver),
    ]
    st_children = b''.join(_block(k, 1, v.encode('utf-16-le') + b'\0\0') for k, v in strings)
    stable = _block('040904B0', 1, b'', st_children)
    sfi = _block('StringFileInfo', 1, b'', stable)
    var = _block('Translation', 0, struct.pack('<HH', 0x409, 0x4B0))
    vfi = _block('VarFileInfo', 1, b'', var)
    return _block('VS_VERSION_INFO', 0, ffi, sfi + vfi, vlen_override=len(ffi))


# ---------------- Resource section builder ----------------
class ResBuilder:
    def __init__(self):
        self.items = []  # (type, name, lang, bytes)

    def add(self, t, name, lang, blob):
        self.items.append((t, name, lang, bytes(blob)))

    def build(self, section_rva):
        types = {}
        for t, n, lang, b in self.items:
            types.setdefault(t, {}).setdefault(n, []).append((lang, b))
        tkeys = sorted([k for k in types if isinstance(k, str)])
        tkeys += sorted([k for k in types if isinstance(k, int)])

        out = bytearray()

        def alloc_dir(nentries):
            o = len(out)
            out.extend(b'\0' * (16 + 8 * nentries))
            return o

        root = alloc_dir(len(tkeys))
        tdirs = {}
        for tk in tkeys:
            names = sorted(types[tk].keys())
            tdirs[tk] = (alloc_dir(len(names)), names)
        ndirs = {}
        for tk in tkeys:
            for nm in sorted(types[tk].keys()):
                langs = types[tk][nm]
                ndirs[(tk, nm)] = (alloc_dir(len(langs)), langs)
        # NOTE: standard 3-level tree (type -> name -> lang -> data entry).
        # Language entries point DIRECTLY at data entries (no extra level).
        # strings
        stroffs = {}
        for tk in tkeys:
            if isinstance(tk, str):
                stroffs[tk] = len(out)
                out.extend(struct.pack('<H', len(tk)) + tk.encode('utf-16-le'))
                while len(out) % 4:
                    out.append(0)
        # data entries
        dents = {}
        for tk in tkeys:
            for nm in sorted(types[tk].keys()):
                for li in range(len(types[tk][nm])):
                    dents[(tk, nm, li)] = len(out)
                    out.extend(b'\0' * 16)
        # blobs
        blobs = {}
        for tk in tkeys:
            for nm in sorted(types[tk].keys()):
                for li, (lang, b) in enumerate(types[tk][nm]):
                    while len(out) % 4:
                        out.append(0)
                    blobs[(tk, nm, li)] = len(out)
                    out.extend(b)

        def fill_dir(doff, entries):
            named = [e for e in entries if e[1]]
            ids = [e for e in entries if not e[1]]
            struct.pack_into('<IIHHHH', out, doff, 0, 0, 0, 0, len(named), len(ids))
            p = doff + 16
            for key, isnamed, tgt, isdir in named + ids:
                struct.pack_into('<II', out, p,
                                 (key | 0x80000000) if isnamed else key,
                                 (tgt | 0x80000000) if isdir else tgt)
                p += 8

        fill_dir(root, [(stroffs[tk] if isinstance(tk, str) else tk,
                         isinstance(tk, str), tdirs[tk][0], True) for tk in tkeys])
        for tk in tkeys:
            doff, names = tdirs[tk]
            fill_dir(doff, [(nm, False, ndirs[(tk, nm)][0], True) for nm in names])
            for nm in names:
                doff2, langs = ndirs[(tk, nm)]
                fill_dir(doff2, [(langs[li][0], False, dents[(tk, nm, li)], False)
                                 for li in range(len(langs))])
                for li, (lang, b) in enumerate(langs):
                    struct.pack_into('<IIII', out, dents[(tk, nm, li)],
                                     section_rva + blobs[(tk, nm, li)], len(b), 0, 0)
        return bytes(out)


# ---------------- PE surgery ----------------
class PE:
    def __init__(self, d):
        self.d = d
        assert d[0:2] == b'MZ', 'not MZ'
        self.pe = struct.unpack('<I', d[0x3C:0x40])[0]
        assert d[self.pe:self.pe + 4] == b'PE\0\0', 'not PE'
        self.coff = self.pe + 4
        self.nsec = struct.unpack('<H', d[self.coff + 2:self.coff + 4])[0]
        self.optsz = struct.unpack('<H', d[self.coff + 16:self.coff + 18])[0]
        self.opt = self.coff + 20
        assert struct.unpack('<H', d[self.opt:self.opt + 2])[0] == 0x20b, 'only PE32+'
        self.sec_align = struct.unpack('<I', d[self.opt + 32:self.opt + 36])[0]
        self.file_align = struct.unpack('<I', d[self.opt + 36:self.opt + 40])[0]
        self.sectab = self.opt + self.optsz

    def sections(self):
        out = []
        for i in range(self.nsec):
            o = self.sectab + i * 40
            name = bytes(self.d[o:o + 8])
            vsize, vaddr, rawsize, rawptr = struct.unpack('<IIII', self.d[o + 8:o + 24])
            chars = struct.unpack('<I', self.d[o + 36:o + 40])[0]
            out.append({'name': name, 'vsize': vsize, 'vaddr': vaddr,
                        'rawsize': rawsize, 'rawptr': rawptr, 'chars': chars, 'off': o})
        return out


def inject(raw_path, out_path, res_items, gui=True):
    d = bytearray(open(raw_path, 'rb').read())
    pe = PE(d)
    secs = pe.sections()
    assert all(s['name'] != b'.rsrc\0\0\0' for s in secs), '.rsrc already exists'

    b = ResBuilder()
    for t, name, lang, blob in res_items:
        b.add(t, name, lang, blob)

    last = secs[-1]
    rva = align_up(last['vaddr'] + max(last['vsize'], last['rawsize']), pe.sec_align)
    raw = align_up(last['rawptr'] + last['rawsize'], pe.file_align)

    blob = b.build(rva)
    vsize = len(blob)
    rawsize = align_up(vsize, pe.file_align)

    new_hdr = pe.sectab + pe.nsec * 40
    first_raw = min(s['rawptr'] for s in secs)
    assert new_hdr + 40 <= first_raw, 'no room for new section header'

    assert len(d) <= raw, 'unexpected overlay data'
    d.extend(b'\0' * (raw - len(d)))
    d.extend(blob)
    d.extend(b'\0' * (rawsize - vsize))

    struct.pack_into('<8sIIIIIIHHI', d, new_hdr, b'.rsrc\0\0\0', vsize, rva,
                     rawsize, raw, 0, 0, 0, 0, 0x40000040)
    struct.pack_into('<H', d, pe.coff + 2, pe.nsec + 1)
    struct.pack_into('<I', d, pe.opt + 56, align_up(rva + align_up(vsize, pe.sec_align), pe.sec_align))
    struct.pack_into('<II', d, pe.opt + 112 + 16, rva, vsize)  # DataDirectory[RESOURCE]
    if gui:
        struct.pack_into('<H', d, pe.opt + 68, 2)  # IMAGE_SUBSYSTEM_WINDOWS_GUI
    struct.pack_into('<I', d, pe.opt + 64, 0)  # checksum

    open(out_path, 'wb').write(d)
    return {'rva': rva, 'raw': raw, 'vsize': vsize, 'rawsize': rawsize}


# ---------------- Validator ----------------
def read_resources(path):
    d = open(path, 'rb').read()
    pe = PE(d)
    dd = pe.opt + 112 + 16
    rva, size = struct.unpack('<II', d[dd:dd + 8])
    assert rva != 0 and size != 0, 'no resource directory'
    secs = pe.sections()
    base = None
    for s in secs:
        if s['vaddr'] <= rva < s['vaddr'] + max(s['vsize'], s['rawsize']):
            base = s['rawptr'] + (rva - s['vaddr'])
    assert base is not None, 'resource rva not mapped'

    def rdir(off):
        n_named, n_id = struct.unpack('<HH', d[base + off + 12:base + off + 16])
        ents = []
        for i in range(n_named + n_id):
            a, b2 = struct.unpack('<II', d[base + off + 16 + i * 8:base + off + 24 + i * 8])
            ents.append((a, b2))
        return n_named, ents

    def rstr(off):
        ln = struct.unpack('<H', d[base + off:base + off + 2])[0]
        return d[base + off + 2:base + off + 2 + ln * 2].decode('utf-16-le')

    found = {}
    _, tents = rdir(0)
    for ta, tb in tents:
        t = rstr(ta & 0x7FFFFFFF) if ta & 0x80000000 else (ta & 0xFFFF)
        assert tb & 0x80000000
        _, nents = rdir(tb & 0x7FFFFFFF)
        for na, nb in nents:
            n = rstr(na & 0x7FFFFFFF) if na & 0x80000000 else (na & 0xFFFF)
            assert nb & 0x80000000
            _, lents = rdir(nb & 0x7FFFFFFF)
            for la, lb in lents:
                lang = la & 0xFFFF
                assert not (lb & 0x80000000), 'resource tree must be 3 levels'
                if True:
                    drva, dsz = struct.unpack('<II', d[base + (lb & 0x7FFFFFFF):base + (lb & 0x7FFFFFFF) + 8])
                    doff = None
                    for s in secs:
                        if s['vaddr'] <= drva < s['vaddr'] + max(s['vsize'], s['rawsize']):
                            doff = s['rawptr'] + (drva - s['vaddr'])
                    assert doff is not None
                    found[(t, n, lang)] = d[doff:doff + dsz]
    subsys = struct.unpack('<H', d[pe.opt + 68:pe.opt + 70])[0]
    return found, subsys, len(secs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('raw')
    ap.add_argument('out')
    ap.add_argument('--icon', required=True)
    ap.add_argument('--manifest', required=True)
    ap.add_argument('--logo', required=True)
    ap.add_argument('--banner', required=True)
    ap.add_argument('--db', required=True)
    ap.add_argument('--ver', default='1.0.0')
    a = ap.parse_args()

    imgs = parse_ico(a.icon)
    print('icon images:', [(i['w'], i['h'], len(i['data'])) for i in imgs])
    grp = group_icon(imgs)
    manifest = open(a.manifest, 'rb').read()
    logo = open(a.logo, 'rb').read()
    banner = open(a.banner, 'rb').read()
    db = open(a.db, 'rb').read()
    ver = version_info(a.ver)

    items = []
    for i, im in enumerate(imgs):
        items.append((RT_ICON, i + 1, LANG, im['data']))
    items.append((RT_GROUP_ICON, 101, LANG, grp))
    items.append((RT_MANIFEST, 1, LANG, manifest))
    items.append((RT_VERSION, 1, LANG, ver))
    items.append(('PNG', 201, LANG, logo))
    items.append(('PNG', 202, LANG, banner))
    items.append((RT_RCDATA, 301, LANG, db))

    info = inject(a.raw, a.out, items)
    print('rsrc: rva=0x%x raw=0x%x vsize=%d' % (info['rva'], info['raw'], info['vsize']))

    found, subsys, nsec = read_resources(a.out)
    print('sections:', nsec, 'subsystem:', subsys, '(2=GUI)')
    assert subsys == 2
    expect = {(t, n, lang): bytes(blob) for t, n, lang, blob in items}
    assert set(found.keys()) == set(expect.keys()), 'resource key mismatch: %s vs %s' % (set(found.keys()), set(expect.keys()))
    for k in expect:
        assert found[k] == expect[k], 'resource data mismatch: %r' % (k,)
    print('validated %d resources, all bytes match.' % len(expect))
    print('OK:', a.out, os.path.getsize(a.out), 'bytes')


if __name__ == '__main__':
    main()

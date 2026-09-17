#!/usr/bin/env python3
"""FPS Booster Pro - full build orchestrator.
Compile (Zig -> Windows x64) -> inject resources -> pad to 100 MB -> zip.
"""
import os
import sys
import struct
import subprocess
import hashlib
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, 'src')
BUILD = os.path.join(ROOT, 'build')
RELEASE = os.path.join(ROOT, 'release')
TARGET_SIZE = 100 * 1024 * 1024  # exactly 100 MiB -> Explorer shows "100 MB"

SOURCES = ['main.cpp', 'strings.cpp', 'util.cpp', 'sysinfo.cpp',
           'tweaks.cpp', 'games.cpp', 'ui.cpp', 'power.cpp', 'net.cpp', 'watch.cpp', 'maxfps.cpp']
LIBS = ['comctl32', 'gdi32', 'gdiplus', 'shell32', 'ole32', 'uuid',
        'powrprof', 'advapi32', 'comdlg32', 'winhttp', 'iphlpapi', 'dxgi', 'ws2_32']


def run(cmd, **kw):
    print('+', ' '.join(cmd))
    r = subprocess.run(cmd, cwd=ROOT, **kw)
    if r.returncode != 0:
        sys.exit('BUILD FAILED: %s' % ' '.join(cmd))


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def check_imports(path):
    """Parse the PE import table and report DLLs (sanity check)."""
    d = open(path, 'rb').read()
    pe = struct.unpack('<I', d[0x3C:0x40])[0]
    nsec = struct.unpack('<H', d[pe + 6:pe + 8])[0]
    optsz = struct.unpack('<H', d[pe + 20:pe + 22])[0]
    opt = pe + 24
    sectab = opt + optsz
    secs = []
    for i in range(nsec):
        o = sectab + i * 40
        vsize, vaddr, rawsize, rawptr = struct.unpack('<IIII', d[o + 8:o + 24])
        secs.append((vaddr, max(vsize, rawsize), rawptr))

    def rva2off(rva):
        for va, sz, rp in secs:
            if va <= rva < va + sz:
                return rp + (rva - va)
        return None

    idd = opt + 112 + 8  # DataDirectory[IMPORT]
    irva, isz = struct.unpack('<II', d[idd:idd + 8])
    assert irva and isz, 'no import directory!'
    dlls = []
    off = rva2off(irva)
    while True:
        ilt, ts, fwd, name_rva, iat = struct.unpack('<IIIII', d[off:off + 20])
        if ilt == ts == fwd == name_rva == iat == 0:
            break
        noff = rva2off(name_rva)
        end = d.index(b'\0', noff)
        dlls.append(d[noff:end].decode('ascii'))
        off += 20
    entry = struct.unpack('<I', d[opt + 16:opt + 20])[0]
    print('entry point RVA: 0x%x' % entry)
    print('imports (%d DLLs): %s' % (len(dlls), ', '.join(dlls)))
    need = ['KERNEL32.dll', 'USER32.dll', 'GDI32.dll', 'GDIPlus.dll',
            'COMCTL32.dll', 'SHELL32.dll', 'ole32.dll', 'POWRPROF.dll',
            'ADVAPI32.dll', 'COMDLG32.dll']
    up = [x.upper() for x in dlls]
    for n in need:
        assert n.upper() in up, 'missing import: %s' % n
    print('import check: all required DLLs present.')


def pad_to_100mb(src, dst):
    data = open(src, 'rb').read()
    assert len(data) < TARGET_SIZE, 'binary already over 100 MB?!'
    header = b'FPSBOOSTEROVERLAY\x00'
    header += struct.pack('<QQI', len(data), TARGET_SIZE, 1)
    header += b'Zero padding to reach exactly 100 MB as requested. Ignored by the Windows loader (overlay).'
    header = header.ljust(512, b'\0')
    with open(dst, 'wb') as f:
        f.write(data)
        f.write(header)
        remaining = TARGET_SIZE - len(data) - len(header)
        chunk = b'\0' * (1 << 20)
        while remaining > 0:
            n = min(remaining, len(chunk))
            f.write(chunk[:n])
            remaining -= n
    final = os.path.getsize(dst)
    assert final == TARGET_SIZE, (final, TARGET_SIZE)
    print('padded: %d -> %d bytes (%.1f MB)' % (len(data), final, final / 1024 / 1024))


def main():
    os.makedirs(BUILD, exist_ok=True)
    os.makedirs(RELEASE, exist_ok=True)

    print('=== [1/5] assets ===')
    run([sys.executable, 'tools/make_assets.py'])

    print('=== [2/5] compile (Zig -> Windows x64) ===')
    raw = os.path.join(BUILD, 'fpsbooster_raw.exe')
    cmd = [sys.executable, '-m', 'ziglang', 'c++', '-target', 'x86_64-windows-gnu',
           '-O2', '-DUNICODE', '-D_UNICODE', '-Wno-nullability-completeness',
           '-Isrc', '-o', raw] + [os.path.join('src', s) for s in SOURCES]
    for lib in LIBS:
        cmd.append('-l' + lib)
    run(cmd)

    print('=== [3/5] inject resources ===')
    full = os.path.join(BUILD, 'fpsbooster_full.exe')
    run([sys.executable, 'tools/inject_resources.py', raw, full,
         '--icon', 'assets/icon.ico', '--manifest', 'res/app.manifest',
         '--logo', 'assets/logo_ui.png', '--banner', 'assets/banner_ui.png',
         '--db', 'data/games_db.json', '--ver', '1.3.0'])

    print('=== [4/5] PE checks + pad to 100 MB ===')
    check_imports(full)
    exe = os.path.join(RELEASE, 'fpsbooster.exe')
    pad_to_100mb(full, exe)

    print('=== [5/5] portable zip ===')
    zpath = os.path.join(RELEASE, 'FPSBooster-v1.3-Portable.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.write(exe, 'fpsbooster.exe')
        z.write(os.path.join(ROOT, 'README.md'), 'README.md')
    print('zip: %.2f MB' % (os.path.getsize(zpath) / 1024 / 1024))

    print()
    print('BUILD OK')
    print('  exe : %s (%d bytes)' % (exe, os.path.getsize(exe)))
    print('  sha256: %s' % sha256(exe))
    print('  zip : %s (%d bytes)' % (zpath, os.path.getsize(zpath)))


if __name__ == '__main__':
    main()

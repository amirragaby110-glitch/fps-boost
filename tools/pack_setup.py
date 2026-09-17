#!/usr/bin/env python3
"""Pack a built setup stub + app payload into the 100 MB setup exe.

Usage: pack_setup.py STUB.exe PAYLOAD.exe OUT.exe
Layout: [stub][payload][zeros][footer64] == exactly 100 MiB.
Used by MSVC/CMake builds (build.py does this internally for Zig builds).
"""
import os
import struct
import sys

TARGET = 100 * 1024 * 1024


def main():
    if len(sys.argv) != 4:
        sys.exit('usage: pack_setup.py STUB.exe PAYLOAD.exe OUT.exe')
    stub = open(sys.argv[1], 'rb').read()
    pay = open(sys.argv[2], 'rb').read()
    footer = b'FPSBOOSTERSETUP1'.ljust(32, b'\0')
    footer += struct.pack('<QQQ', len(stub), len(pay), 1)
    footer = footer.ljust(64, b'\0')
    with open(sys.argv[3], 'wb') as f:
        f.write(stub)
        f.write(pay)
        zeros = TARGET - len(stub) - len(pay) - len(footer)
        assert zeros > 0, 'inputs too big for 100 MB?!'
        chunk = b'\0' * (1 << 20)
        while zeros > 0:
            n = min(zeros, len(chunk))
            f.write(chunk[:n])
            zeros -= n
        f.write(footer)
    assert os.path.getsize(sys.argv[3]) == TARGET
    print('setup packed: %s (%d bytes)' % (sys.argv[3], TARGET))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Make a v6 RK05 image bootable.

The TUHS Dennis_v6 images (v6root etc.) ship WITHOUT a boot block — block 0 is
garbage, so `boot rk0` double-faults on any emulator (verified on SIMH too).
The real boot block lives inside the filesystem at /usr/mdec/rkuboot (a.out
with 16-byte header). This extracts it and installs it at block 0.

Usage: mkboot.py v6root.dsk v6boot.dsk
"""
import struct, sys, os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from v6fs import V6FS

def main():
    src, dst = sys.argv[1], sys.argv[2]
    fs = V6FS(src)
    boot = fs.read_file(fs.lookup('/usr/mdec/rkuboot'))
    magic = struct.unpack('<H', boot[:2])[0]
    if magic == 0o407:
        boot = boot[16:]
    if len(boot) > 512:
        sys.exit(f"boot block too big: {len(boot)}")
    boot = boot + b'\0' * (512 - len(boot))
    img = bytearray(open(src, 'rb').read())
    img[0:512] = boot
    open(dst, 'wb').write(img)
    w0 = struct.unpack('<H', boot[:2])[0]
    print(f"{dst}: installed rkuboot at block 0 (first word {w0:06o})")

main()

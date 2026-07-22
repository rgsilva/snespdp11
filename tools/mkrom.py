#!/usr/bin/env python3
"""Prepare a v6 RK05 image for cart ROM.

- Walks the v6 free-block chain and ZEROES all free blocks: deterministic
  output, compresses well, and scrubs deleted-file remnants (release hygiene —
  see emu8086's copyrighted-file scrub precedent).
- Pads to a 32 KB multiple (SNES bank granularity; exact LoROM layout is M3's
  call — block N stays at byte N*512 so the emulator's rk0 read path is
  identity-mapped).
- Prints an fs usage report (the numbers that decide ROM size).

Usage: mkrom.py in.dsk out.rom
"""
import struct, sys

BLK = 512

def words(b):
    return struct.unpack(f'<{len(b)//2}H', b)

def main():
    src, dst = sys.argv[1], sys.argv[2]
    img = bytearray(open(src, 'rb').read())
    nblk = (len(img) + BLK - 1) // BLK

    sb = img[BLK:2*BLK]
    isize, fsize, nfree = struct.unpack_from('<HHH', sb, 0)
    free = list(struct.unpack_from('<100H', sb, 6))

    # walk the free chain: free[0] links to a block holding the next batch
    freeset = set()
    batch_nfree, batch = nfree, free
    while True:
        link = batch[0]
        for b in batch[1:batch_nfree]:
            if b: freeset.add(b)
        if not link:
            break
        freeset.add(link)
        blk = img[link*BLK:(link+1)*BLK]
        w = words(blk[:202])
        batch_nfree, batch = w[0], list(w[1:101])

    used = fsize - len(freeset)
    print(f"fs: isize={isize} blocks of inodes, fsize={fsize} blocks "
          f"({fsize*BLK//1024} KB), free={len(freeset)}, used={used} "
          f"({used*BLK//1024} KB)")

    zeroed = 0
    for b in freeset:
        if b < fsize and b*BLK < len(img):
            if any(img[b*BLK:(b+1)*BLK]):
                zeroed += 1
            img[b*BLK:(b+1)*BLK] = b'\0' * BLK

    pad = (-len(img)) % (32*1024)
    img += b'\0' * pad
    open(dst, 'wb').write(img)
    print(f"{dst}: {len(img)} bytes ({len(img)//1024} KB, "
          f"{len(img)//(32*1024)} x 32K banks), {zeroed} free blocks scrubbed")

main()

#!/usr/bin/env python3
"""Read-only Unix v6 filesystem walker. Needed by mkrom (M2); also handy for
pulling /usr/mdec/rkuboot and kernels out of disk images.

Usage:
  v6fs.py IMG ls /path            list directory
  v6fs.py IMG cat /path > out     extract file
  v6fs.py IMG stat /path          inode details
"""
import struct, sys

BLK = 512

class V6FS:
    def __init__(self, path):
        self.f = open(path, 'rb')

    def blk(self, n):
        self.f.seek(n * BLK)
        d = self.f.read(BLK)
        return d + b'\0' * (BLK - len(d))

    def inode(self, inum):
        # inodes start at block 2; 16 per block; inode 1 is the first slot
        b = self.blk(2 + (inum - 1) // 16)
        off = ((inum - 1) % 16) * 32
        flags, nlinks, uid, gid, size0, size1 = struct.unpack_from('<HBBBBH', b, off)
        # note: layout is flags(w) nlinks(b) uid(b) gid(b) size0(b) size1(w)
        addr = struct.unpack_from('<8H', b, off + 8)
        size = (size0 << 16) | size1
        return {'flags': flags, 'size': size, 'addr': addr, 'inum': inum}

    def data_blocks(self, ino):
        addr = ino['addr']
        nblk = (ino['size'] + BLK - 1) // BLK
        if not (ino['flags'] & 0o10000):          # small file: direct
            for i in range(nblk):
                yield addr[i]
            return
        # large: addr[0..6] indirect, addr[7] double-indirect
        got = 0
        for i in range(7):
            if got >= nblk: return
            if addr[i] == 0:
                for _ in range(min(256, nblk - got)):
                    yield 0; got += 1
                continue
            ind = struct.unpack('<256H', self.blk(addr[i]))
            for b in ind:
                if got >= nblk: return
                yield b; got += 1
        if addr[7]:
            dind = struct.unpack('<256H', self.blk(addr[7]))
            for ib in dind:
                if got >= nblk: return
                ind = struct.unpack('<256H', self.blk(ib)) if ib else [0]*256
                for b in ind:
                    if got >= nblk: return
                    yield b; got += 1

    def read_file(self, ino):
        out = b''
        for b in self.data_blocks(ino):
            out += self.blk(b) if b else b'\0' * BLK
        return out[:ino['size']]

    def readdir(self, ino):
        d = self.read_file(ino)
        out = []
        for off in range(0, len(d) - 15, 16):
            inum, name = struct.unpack_from('<H14s', d, off)
            if inum:
                out.append((inum, name.rstrip(b'\0').decode('latin1')))
        return out

    def lookup(self, path):
        ino = self.inode(1)
        for part in path.strip('/').split('/'):
            if not part:
                continue
            for inum, name in self.readdir(ino):
                if name == part:
                    ino = self.inode(inum)
                    break
            else:
                raise FileNotFoundError(path)
        return ino

def typ(flags):
    t = flags & 0o60000
    return {0: '-', 0o40000: 'd', 0o20000: 'c', 0o60000: 'b'}[t]

def main():
    img, cmd, path = sys.argv[1], sys.argv[2], (sys.argv[3] if len(sys.argv) > 3 else '/')
    fs = V6FS(img)
    ino = fs.lookup(path)
    if cmd == 'ls':
        if ino['flags'] & 0o40000:
            for inum, name in fs.readdir(ino):
                i = fs.inode(inum)
                print(f"{typ(i['flags'])} {i['size']:7d}  {name}")
        else:
            print(f"{typ(ino['flags'])} {ino['size']:7d}  {path}")
    elif cmd == 'cat':
        sys.stdout.buffer.write(fs.read_file(ino))
    elif cmd == 'stat':
        print(ino)

main()

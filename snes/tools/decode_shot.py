#!/usr/bin/env python3
"""Decode 'SHOT name hex...' lines from a Mesen testrunner log into PNG files.
Usage: decode_shot.py logfile outdir"""
import sys, os
log, outdir = sys.argv[1], sys.argv[2]
os.makedirs(outdir, exist_ok=True)
n = 0
for ln in open(log, errors='ignore'):
    if ln.startswith('SHOT '):
        _, name, hexdata = ln.split(' ', 2)
        open(os.path.join(outdir, name), 'wb').write(bytes.fromhex(hexdata.strip()))
        print("wrote", os.path.join(outdir, name))
        n += 1
if not n:
    print("no SHOT lines found")

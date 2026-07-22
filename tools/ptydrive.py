#!/usr/bin/env python3
"""Tiny expect-driver over a pty. Drives simh or our pdp11 emulator through
boot dialogues and captures the transcript.

Usage: ptydrive.py script.txt -- prog arg1 arg2 ...

Script lines:
    expect <python-regex>     wait (against the tail of output) up to timeout
    send <text>               send text ('\\r', '\\n', '\\x04' escapes OK)
    timeout <seconds>         set timeout for subsequent expects (default 60)
    sleep <seconds>           just wait
    # comment / blank         ignored
Exit 0 if all expects matched; 1 on timeout (transcript still printed).
"""
import os, pty, re, select, subprocess, sys, time

def main():
    sep = sys.argv.index('--')
    script_path = sys.argv[1]
    argv = sys.argv[sep + 1:]

    steps = []
    for ln in open(script_path):
        ln = ln.rstrip('\n')
        if not ln.strip() or ln.strip().startswith('#'):
            continue
        cmd, _, arg = ln.partition(' ')
        steps.append((cmd, arg))

    mfd, sfd = pty.openpty()
    proc = subprocess.Popen(argv, stdin=sfd, stdout=sfd, stderr=sfd,
                            close_fds=True)
    os.close(sfd)

    buf = b''
    pos = 0                      # consumed-up-to marker for expect
    timeout = 60.0
    ok = True

    def read_avail(deadline):
        nonlocal buf
        while time.time() < deadline:
            r, _, _ = select.select([mfd], [], [], 0.2)
            if mfd in r:
                try:
                    d = os.read(mfd, 4096)
                except OSError:
                    return False
                if not d:
                    return False
                buf += d
                sys.stdout.buffer.write(d)
                sys.stdout.flush()
                return True
            if proc.poll() is not None:
                return False
        return None  # timeout slice with no data

    for cmd, arg in steps:
        if cmd == 'timeout':
            timeout = float(arg)
        elif cmd == 'sleep':
            end = time.time() + float(arg)
            while time.time() < end:
                read_avail(time.time() + 0.2)
        elif cmd == 'send':
            data = arg.encode().decode('unicode_escape').encode('latin1')
            os.write(mfd, data)
        elif cmd == 'expect':
            pat = re.compile(arg.encode())
            deadline = time.time() + timeout
            while True:
                m = pat.search(buf, pos)
                if m:
                    pos = m.end()
                    break
                if time.time() >= deadline:
                    sys.stdout.write(f"\n[ptydrive: TIMEOUT waiting for {arg!r}]\n")
                    ok = False
                    break
                read_avail(deadline)
            if not ok:
                break

    time.sleep(0.3)
    read_avail(time.time() + 0.5)
    proc.terminate()
    sys.stdout.write(f"\n[ptydrive: {'OK' if ok else 'FAILED'}]\n")
    sys.exit(0 if ok else 1)

main()

#!/usr/bin/env python3
"""Minimal read-only ISO9660 walker/extractor for PS2 discs (no RockRidge/Joliet
needed — PS2 discs use plain ISO9660 8.3-ish names like "MAIN.SCM;1").

Usage:
  python3 iso9660.py list   <iso>                 # tree listing
  python3 iso9660.py extract <iso> <path> <outfile>  # e.g. /DATA/MAIN.SCM;1
"""
import struct, sys

SS = 2048


def _sec(f, n, c=1):
    f.seek(n * SS)
    return f.read(SS * c)


def _find_pvd(f):
    for s in range(16, 32):
        d = _sec(f, s)
        if d[1:6] == b'CD001' and d[0] == 1:
            return d
    raise RuntimeError("no Primary Volume Descriptor found")


def _parse_dirent(rec):
    lba = struct.unpack('<I', rec[2:6])[0]
    size = struct.unpack('<I', rec[10:14])[0]
    return lba, size


def _listdir(f, lba, size):
    data = _sec(f, lba, (size + SS - 1) // SS)[:size]
    off = 0
    out = []
    while off < len(data):
        l = data[off]
        if l == 0:
            off = (off // SS + 1) * SS
            if off >= len(data):
                break
            continue
        rec = data[off:off + l]
        elba, esize = _parse_dirent(rec)
        flags = rec[25]
        nlen = rec[32]
        name = rec[33:33 + nlen].decode('latin1')
        out.append((name, elba, esize, flags))
        off += l
    return out


def root_dir(f):
    pvd = _find_pvd(f)
    volid = pvd[40:72].decode('latin1').strip()
    return volid, _parse_dirent(pvd[156:156 + 34])


def walk(f, lba, size, path="", depth=0, max_depth=8):
    for name, elba, esize, flags in _listdir(f, lba, size):
        if name in ('\x00', '\x01'):
            continue
        p = path + "/" + name
        if flags & 2:
            yield ("D", esize, p, elba)
            if depth < max_depth:
                yield from walk(f, elba, esize, p, depth + 1, max_depth)
        else:
            yield ("F", esize, p, elba)


def find(f, target_path):
    """target_path like '/DATA/MAIN.SCM' — ';1' version suffix optional/ignored."""
    target = target_path.strip('/').upper()
    _, (rl, rs) = root_dir(f)
    for kind, size, path, lba in walk(f, rl, rs):
        p = path.strip('/').upper()
        if p == target or p.split(';')[0] == target:
            return kind, size, path, lba
    return None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    cmd, iso = sys.argv[1], sys.argv[2]
    with open(iso, 'rb') as f:
        if cmd == 'list':
            volid, (rl, rs) = root_dir(f)
            print("volid:", volid)
            for kind, size, path, lba in walk(f, rl, rs):
                print("%s %10d %-40s lba=%d" % (kind, size, path, lba))
        elif cmd == 'extract':
            path, outfile = sys.argv[3], sys.argv[4]
            hit = find(f, path)
            if not hit:
                print("not found:", path, file=sys.stderr)
                sys.exit(1)
            kind, size, p, lba = hit
            if kind != 'F':
                print("not a file:", path, file=sys.stderr)
                sys.exit(1)
            data = _sec(f, lba, (size + SS - 1) // SS)[:size]
            open(outfile, 'wb').write(data)
            print("wrote %d bytes to %s (from %s)" % (size, outfile, p))
        else:
            print(__doc__)
            sys.exit(1)


if __name__ == '__main__':
    main()

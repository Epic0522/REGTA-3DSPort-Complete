"""Library for exploring a stripped PS2 (MIPS R5900 "EE") boot ELF, e.g. the
SLUS_XXX.XX main executable extracted from a GTA PS2 disc via iso9660.py.

Section headers and the linker gp value are parsed directly from the ELF —
nothing is hardcoded, so this works unmodified across disc revisions/regions
as long as the layout is a standard 32-bit MIPS ELF.

Usage as a library (from a Python REPL or another script, run from this
directory so the plain `import elf_tools` works):

    import elf_tools as E
    E.load("/tmp/opencode/SLUS_214.23")
    E.dis(0x118380, 60)               # disassemble 60 insns at VA
    E.find_addr_refs(0x3cf7c8)        # find lui/ori|addiu pairs building this VA
    E.words(0x3a7020, 5)              # read 5 raw words (e.g. a jump table)
    E.gp                              # linker $gp value (needed for gp-relative loads)

Or from the CLI:
    python3 elf_tools.py <elf> dis <hexva> <count>
    python3 elf_tools.py <elf> words <hexva> <count>
    python3 elf_tools.py <elf> refs <hexva>

Requires `pip install capstone` for disassembly (words/refs work without it).
"""
import struct
import sys

try:
    from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN
    _HAVE_CAPSTONE = True
except ImportError:
    _HAVE_CAPSTONE = False

D = None          # raw file bytes
SEC = {}          # name -> (va, off, size)
gp = None         # linker $gp value, from .reginfo
_md = None


def load(path):
    """Parse ELF header + section headers + .reginfo gp value."""
    global D, SEC, gp, _md
    D = open(path, 'rb').read()
    if D[:4] != b'\x7fELF':
        raise ValueError("not an ELF file")
    ei_class = D[4]
    if ei_class != 1:
        raise ValueError("only 32-bit ELF supported")
    # ELF32 header (little-endian, matches PS2/MIPS EE)
    e_shoff, = struct.unpack_from('<I', D, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', D, 0x2e)
    # section header string table
    shstr_off, = struct.unpack_from('<I', D, e_shoff + e_shstrndx * e_shentsize + 0x10)
    SEC = {}
    reginfo_off = reginfo_size = None
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        name_off, sh_type, sh_flags, sh_addr, sh_off, sh_size = struct.unpack_from(
            '<IIIIII', D, base)
        name = D[shstr_off + name_off: D.index(b'\x00', shstr_off + name_off)].decode()
        if sh_addr or sh_type == 8:  # SHT_NOBITS(8)=.bss etc still gets a VA
            SEC[name] = (sh_addr, sh_off, sh_size)
        if name == '.reginfo':
            reginfo_off, reginfo_size = sh_off, sh_size
    if reginfo_off is not None:
        # Elf32_RegInfo: ri_gprmask(4) + ri_cprmask[4](16) + ri_gp_value(4)
        gp, = struct.unpack_from('<I', D, reginfo_off + 20)
    if _HAVE_CAPSTONE:
        _md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 + CS_MODE_LITTLE_ENDIAN)
        _md.detail = False
    return SEC, gp


def va2off(va):
    for name, (v, o, s) in SEC.items():
        if v <= va < v + s:
            return o + (va - v)
    return None


def off2va(off):
    for name, (v, o, s) in SEC.items():
        if o <= off < o + s:
            return v + (off - o)
    return None


def dis(va, count=40):
    """Print `count` instructions starting at VA. Falls back to raw
    `.word` for anything capstone's MIPS32 mode can't decode (common for
    PS2 EE-specific 128-bit/MMI instructions — use d2-style skip-on-fail
    if you need to push through those)."""
    if not _HAVE_CAPSTONE:
        raise RuntimeError("pip install capstone")
    off = va2off(va)
    for i in _md.disasm(D[off:off + count * 4], va):
        print("%08x  %-10s %s" % (i.address, i.mnemonic, i.op_str))


def dis_robust(va, count):
    """Like dis(), but never stops at an undecodable instruction — prints
    `.word 0x...` for it and advances 4 bytes. Essential for PS2 EE code,
    which mixes regular MIPS III with 128-bit MMI/COP2 instructions that
    capstone's plain MIPS32 mode cannot decode."""
    if not _HAVE_CAPSTONE:
        raise RuntimeError("pip install capstone")
    a = va
    end = va + 4 * count
    while a < end:
        o = va2off(a)
        got = False
        for i in _md.disasm(D[o:o + 4], a):
            print("%08x  %-10s %s" % (i.address, i.mnemonic, i.op_str))
            got = True
        if not got:
            print("%08x  .word      0x%08x" % (a, struct.unpack('<I', D[o:o + 4])[0]))
        a += 4


def words(va, n):
    """Read n raw little-endian uint32 words starting at VA (e.g. jump tables,
    RGBA colour tables, other data that isn't instructions)."""
    off = va2off(va)
    return struct.unpack('<%dI' % n, D[off:off + 4 * n])


# MIPS opcode fields for a load/store/immediate-arith instruction, used by
# find_addr_refs: opcode -> reads rs (base) + imm.
_IMM_OPS = {
    0x09,  # addiu
    0x0d,  # ori
    0x23, 0x27, 0x21, 0x25, 0x20, 0x24,  # lw/lhu/lh/lbu/lb variants
    0x2b, 0x29, 0x28, 0x31, 0x35, 0x39, 0x3d,  # sw/sh/sb/lwc1/lwc2/swc1/swc2
}


def find_addr_refs(target, section='.text'):
    """Scan `section` for `lui $rt,HI16 ; <imm-op> $rt,LO16($rt)` pairs (with
    up to ~12 instructions of slack between them, matching typical -O2 gcc
    scheduling) that together materialize the 32-bit address `target`.
    Returns a list of VAs of the `lui`. This is the standard way to find who
    references a given data address (e.g. a colour-constant table) in a
    stripped MIPS binary with no relocations left."""
    hi = (target >> 16) & 0xffff
    lo = target & 0xffff
    if lo & 0x8000:
        hi = (hi + 1) & 0xffff
    slo = lo - 0x10000 if lo & 0x8000 else lo
    v, o, s = SEC[section]
    t = D[o:o + s]
    res = []
    for i in range(0, len(t) - 4, 4):
        w, = struct.unpack('<I', t[i:i + 4])
        if (w >> 26) == 0x0f and (w & 0xffff) == hi:  # lui
            rt = (w >> 16) & 0x1f
            for j in range(i + 4, min(i + 4 * 12, len(t) - 4), 4):
                w2, = struct.unpack('<I', t[j:j + 4])
                op = w2 >> 26
                if op in _IMM_OPS:
                    rs = (w2 >> 21) & 0x1f
                    imm = w2 & 0xffff
                    if rs == rt and (imm == lo or (op == 0x09 and
                                                    (imm - 0x10000 if imm & 0x8000 else imm) == slo)):
                        res.append(v + i)
                        break
    return res


if __name__ == '__main__':
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    path, cmd = sys.argv[1], sys.argv[2]
    load(path)
    if cmd == 'dis':
        dis_robust(int(sys.argv[3], 16), int(sys.argv[4]))
    elif cmd == 'words':
        for w in words(int(sys.argv[3], 16), int(sys.argv[4])):
            print(hex(w))
    elif cmd == 'refs':
        for va in find_addr_refs(int(sys.argv[3], 16)):
            print(hex(va))
    else:
        print(__doc__)

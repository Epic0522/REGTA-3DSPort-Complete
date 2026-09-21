"""Minimal LCS main.scm disassembler.

Parses opcode names/argument shapes straight out of the reLCS source
(`stories/src/control/ScriptDebug.cpp`'s `REGISTER_COMMAND` table), so it
stays in sync with whatever opcode set the port actually implements, and
decodes self-describing parameters per `CRunningScript::CollectParameters`
(`stories/src/control/Script.cpp`) using the `ARGUMENT_*` enum from
`stories/src/control/Script.h`.

CAVEAT (learned the hard way): linear disassembly from a known-good anchor
(e.g. right after the initial GOTO) is reliable. Scanning forward from an
arbitrary byte offset and "validating" a candidate instruction stream is
NOT reliable — it produces false negatives/positives because SCM opcodes
have no alignment or synchronization markers. Prefer linear disassembly
from `first_instruction_offset()` and stop trusting the stream once you
hit a decode failure, rather than trying to resync.

Usage:
    import scm_disasm as S
    names, args = S.load_opcode_table("/path/to/stories/src/control/ScriptDebug.cpp")
    d = S.load_scm("/path/to/main.scm")
    start = S.first_instruction_offset(d)   # after the leading GOTO
    for opname, params, next_off in S.disasm_linear(d, names, args, start):
        print(opname, params)
"""
import re
import struct

# ARGUMENT_* enum, stories/src/control/Script.h ARGUMENT_* + NUM_* constants
END, IZ, FZ, F1, F2, F3, I32, I8, I16, FL = range(10)
NUM_TIMERS = 2
NUM_LOCAL_VARS = 96
NUM_GLOBAL_SLOTS = 26
TIMER = 10
LOCAL = TIMER + NUM_TIMERS            # 12
LOCAL_ARRAY = LOCAL + NUM_LOCAL_VARS  # 108
GLOBAL = LOCAL_ARRAY + NUM_LOCAL_VARS  # 204
GLOBAL_ARRAY = GLOBAL + NUM_GLOBAL_SLOTS  # 230
MAX_ARG = GLOBAL_ARRAY + NUM_GLOBAL_SLOTS  # 256


def load_opcode_table(scriptdebug_cpp_path):
    """Returns (names, args) where names[i] is 'COMMAND_FOO' and args[i] is
    the list of INPUT+OUTPUT argument-type tokens (e.g. 'ARGTYPE_INT'),
    indexed by opcode number (REGISTER_COMMAND call order == opcode index)."""
    src = open(scriptdebug_cpp_path).read()
    ents = re.findall(
        r'REGISTER_COMMAND\((COMMAND_[A-Z0-9_]+),\s*INPUT_ARGUMENTS\(([^)]*)\),'
        r'\s*OUTPUT_ARGUMENTS\(([^)]*)\)', src)
    names, args = [], []
    for n, i, o in ents:
        names.append(n)
        a = [x.strip() for x in (i + ',' + o).split(',') if x.strip()]
        args.append(a)
    return names, args


def load_scm(path):
    """Returns script-space bytes with the 8-byte file header (two int32:
    MainScriptSize, nLargestMissionSize) stripped, so offset 0 in the
    returned bytes == ScriptSpace offset 0 used by opcode GOTO/JSR targets."""
    return open(path, 'rb').read()[8:]


def first_instruction_offset(scm_bytes):
    """Script-space byte 0 is always a GOTO past the mission-table area to
    the real start of mainline code; return that target offset.

    NOTE: the GOTO's argument is a self-describing param (type byte at
    offset 2, then payload) like any other — it is NOT a bare int32
    immediately after the 2-byte opcode. Decode it with _read_param rather
    than reading a raw int32 at offset 2, or you'll get a garbage offset
    (verified: raw int32-at-offset-2 gives 4475910 instead of the correct
    17484 on real main.scm)."""
    op = struct.unpack_from('<H', scm_bytes, 0)[0] & 0x7fff
    assert op == 0x002, "expected GOTO (opcode 2) at offset 0, got %#x" % op
    r = _read_param(scm_bytes, 2)
    assert r is not None and r[2] == 'lit', "GOTO target must decode as a literal"
    return r[0]


def _read_param(d, i):
    """Returns (value_or_None, next_i, kind) or None on decode failure.
    kind is 'lit' for literals (value is the actual int), or one of
    'var'/'larr'/'garr'/'glob' for variable references (value is None —
    resolving the exact variable index isn't needed for colour/const
    archaeology; extend here if you need it)."""
    if i >= len(d):
        return None
    t = d[i]
    i += 1
    if t in (IZ, FZ):
        return (0, i, 'lit')
    if t == F1:
        return (d[i] << 24, i + 1, 'lit')
    if t == F2:
        return (struct.unpack_from('<H', d, i)[0] << 16, i + 2, 'lit')
    if t == F3:
        return ((d[i] << 8) | (struct.unpack_from('<H', d, i + 1)[0] << 16), i + 3, 'lit')
    if t in (I32, FL):
        return (struct.unpack_from('<i', d, i)[0], i + 4, 'lit')
    if t == I8:
        return (struct.unpack_from('<b', d, i)[0], i + 1, 'lit')
    if t == I16:
        return (struct.unpack_from('<h', d, i)[0], i + 2, 'lit')
    if t == END:
        return None
    if t >= MAX_ARG:
        return None
    if t >= GLOBAL_ARRAY:
        return (None, i + 3, 'garr')
    if t >= GLOBAL:
        return (None, i + 1, 'glob')
    if t >= LOCAL_ARRAY:
        return (None, i + 2, 'larr')
    if t >= TIMER:
        return (None, i, 'var')
    return None


def decode_one(d, i, names, args):
    """Decode a single instruction at script-space offset i.
    Returns (opcode_name, params, next_i) or None if decoding failed
    (bad opcode index or a param that ran past a known type)."""
    if i + 2 > len(d):
        return None
    op = struct.unpack_from('<H', d, i)[0]
    i += 2
    real = op & 0x7fff
    if real >= len(names):
        return None
    params = []
    for a in args[real]:
        if a in ('ARGTYPE_STRING', 'ARGTYPE_TEXT_LABEL'):
            params.append(('str', d[i:i + 8]))
            i += 8
            continue
        r = _read_param(d, i)
        if r is None:
            return None
        params.append((r[2], r[0]))
        i = r[1]
    return (names[real], params, i)


def disasm_linear(d, names, args, start, end=None):
    """Generator yielding (opname, params, next_offset) linearly from
    `start`, stopping (StopIteration) at the first decode failure or `end`.
    This is the reliable mode — see module docstring caveat about scanning."""
    i = start
    end = end if end is not None else len(d)
    while i < end:
        r = decode_one(d, i, names, args)
        if r is None:
            return
        opname, params, nexti = r
        yield (opname, params, nexti)
        i = nexti

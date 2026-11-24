"""Flash and RAM a compiled object takes, read straight from ELF section headers (works on .o and .a, 32/64-bit).
   python3 elfsize.py file.a|file.o ..."""
import struct, sys

def elf_sections(b):
    if b[:4] != b"\x7fELF": return None
    is64 = b[4] == 2; le = "<" if b[5] == 1 else ">"
    if is64: shoff, = struct.unpack_from(le + "Q", b, 0x28); shentsize, shnum = struct.unpack_from(le + "HH", b, 0x3A)
    else: shoff, = struct.unpack_from(le + "I", b, 0x20); shentsize, shnum = struct.unpack_from(le + "HH", b, 0x2E)
    out = []
    for i in range(shnum):
        o = shoff + i * shentsize
        if is64: name, typ, flags, _, _, size = struct.unpack_from(le + "IIQQQQ", b, o)
        else: name, typ, flags, _, _, size = struct.unpack_from(le + "IIIIII", b, o)
        out.append((typ, flags, size))
    return out

def members(path):
    b = open(path, "rb").read()
    if b[:8] != b"!<arch>\n": yield path, b; return
    o = 8
    while o + 60 <= len(b):
        name = b[o:o + 16].decode().strip(); size = int(b[o + 48:o + 58].decode().strip())
        yield name, b[o + 60:o + 60 + size]; o += 60 + size + (size & 1)

def undefined(path):
    """Symbols the object needs from elsewhere (runtime helpers, C library)."""
    need, have = set(), set()
    for name, b in members(path):
        if b[:4] != b"\x7fELF": continue
        is64 = b[4] == 2; le = "<" if b[5] == 1 else ">"
        if is64: shoff, = struct.unpack_from(le + "Q", b, 0x28); shentsize, shnum = struct.unpack_from(le + "HH", b, 0x3A)
        else: shoff, = struct.unpack_from(le + "I", b, 0x20); shentsize, shnum = struct.unpack_from(le + "HH", b, 0x2E)
        secs = []
        for i in range(shnum):
            o = shoff + i * shentsize
            if is64: nm, typ, fl, ad, off, size, link, info, al, es = struct.unpack_from(le + "IIQQQQIIQQ", b, o)
            else: nm, typ, fl, ad, off, size, link, info, al, es = struct.unpack_from(le + "IIIIIIIIII", b, o)
            secs.append((typ, off, size, link, es))
        for typ, off, size, link, es in secs:
            if typ != 2: continue                               # SHT_SYMTAB
            stoff = secs[link][1]
            for k in range(1, size // es):
                o = off + k * es
                if is64: nm, info, other, shndx = struct.unpack_from(le + "IBBH", b, o)
                else: nm, val, sz, info, other, shndx = struct.unpack_from(le + "IIIBBH", b, o)
                name = b[stoff + nm: b.index(b"\0", stoff + nm)].decode()
                if not name or (info >> 4) == 0: continue          # local symbols
                (need if shndx == 0 else have).add(name)
    return sorted(need - have)

def sizes(path):
    t = {"text": 0, "rodata": 0, "data": 0, "bss": 0}
    for name, b in members(path):
        s = elf_sections(b)
        if not s: continue
        for typ, flags, size in s:
            if not flags & 2: continue                  # SHF_ALLOC: ends up on the chip
            if typ == 8: t["bss"] += size               # SHT_NOBITS
            elif flags & 4: t["text"] += size           # SHF_EXECINSTR
            elif flags & 1: t["data"] += size           # SHF_WRITE
            else: t["rodata"] += size
    return t

for p in sys.argv[1:]:
    t = sizes(p)
    print(f"  needs from outside: {' '.join(undefined(p)) or 'nothing'}")
    print(f"{p}: flash {t['text'] + t['rodata'] + t['data']:,} B (code {t['text']:,}, constants {t['rodata']:,}, initial data {t['data']:,}); "
          f"static RAM {t['data'] + t['bss']:,} B")

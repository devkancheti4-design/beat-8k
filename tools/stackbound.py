"""stackbound.py file.s file.su : the deepest chain of the solver's own stack frames, from the compiler's output.
file.s is the assembly (-S) and file.su the frame sizes (-fstack-usage) of the same compilation. Calls are read from
the assembly (bl / call / rcall / b to another function); the code has no recursion and no calls through pointers.
Calls to the compiler's arithmetic helpers (names starting with __) are listed, not followed: their frames are theirs."""
import re, sys
asm, su = sys.argv[1], sys.argv[2]
frame = {}
for l in open(su):
    p = l.rstrip('\n').split('\t')
    frame[p[0].split(':')[-1]] = int(p[1])
calls, cur, helpers = {}, None, set()
lab = re.compile(r'^([A-Za-z_][\w.]*):')
call = re.compile(r'^\s+(?:bl|blx|b|b\.w|call|rcall|jmp|rjmp|callq|jmpq)\s+([A-Za-z_][\w.]*)')
funcs = set(frame)
for l in open(asm):
    m = lab.match(l)
    if m:
        name = m.group(1).lstrip('_') if m.group(1).lstrip('_') in funcs else m.group(1)
        if name in funcs: cur = name; calls.setdefault(cur, set())
        continue
    m = call.match(l)
    if m and cur:
        t = m.group(1)
        if t not in funcs and t.startswith('_') and t[1:] in funcs: t = t[1:]      # Mach-O names
        if t in funcs and t != cur: calls[cur].add(t)
        elif t.startswith('__') or t.startswith('mem'): helpers.add(t)
def depth(f, seen=()):
    if f in seen: raise SystemExit('recursion at ' + f)
    best, chain = 0, []
    for g in calls.get(f, ()):
        d, c = depth(g, seen + (f,))
        if d > best: best, chain = d, c
    return frame.get(f, 0) + best, [f] + chain
for root in sys.argv[3:]:
    d, c = depth(root)
    print(f"{root}: {d} bytes  ({' > '.join(f'{x} {frame.get(x, 0)}' for x in c)})")
print("helpers called (not followed):", ' '.join(sorted(helpers)) or 'none')

"""cmp_uno.py simulator-log mac-answers.txt : the simulated Uno's answers against this computer's build of the same
configuration (kind of answer and both answers must be the same)."""
import re, sys
mac = {}
for l in open(sys.argv[2]):
    m = re.match(r'(p\d+) (\S+)\s+# (\w+)', l)
    if m: mac[m.group(1)] = (m.group(3), m.group(2))
same = diff = 0; total = 0.0
for l in open(sys.argv[1]):
    t = re.search(r'\[problem \d+: ([\d.]+) s', l)
    if t: total += float(t.group(1))
    m = re.match(r'(p\d+) \w+\s+by=(\w+) ans=(\S+)', l)
    if not m: continue
    pid, by, ans = m.groups()
    want = mac.get(pid)
    ok = want is not None and want[0] == by and (want[1] == ans or (by == 'CANNOT' and want[1] == 'CANNOT'))
    same += ok; diff += not ok
    if not ok: print('  DIFFERENT', pid, by, ans, 'computer:', want)
print(f"{same} of {same + diff} answers the same as this computer's build; {total:.0f} s of chip time")

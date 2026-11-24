"""avrprof.py profile.txt program.elf: chip time by function, from avrsim's AVRSIM_PROFILE output"""
import sys, subprocess, os, bisect
nm = os.path.expanduser('~/Library/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/avr-nm')
syms = []
for l in subprocess.check_output([nm, '-n', sys.argv[2]], text=True).splitlines():
    p = l.split()
    if len(p) == 3 and p[1] in 'tTwW': syms.append((int(p[0], 16), p[2]))
syms.sort(); addrs = [a for a, _ in syms]
tot = {}; total = 0
for l in open(sys.argv[1]):
    a, c = map(int, l.split()); i = bisect.bisect_right(addrs, a) - 1
    name = syms[i][1] if i >= 0 else '?'; tot[name] = tot.get(name, 0) + c; total += c
for name, c in sorted(tot.items(), key=lambda t: -t[1])[:int(sys.argv[3]) if len(sys.argv) > 3 else 14]:
    print(f"  {100 * c / total:5.1f}%  {c / 16e6:8.1f} s  {name}")

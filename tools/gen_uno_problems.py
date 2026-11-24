"""gen_uno_problems.py problems.json answers.json id,id,... > problems_gen.h : a problem table for the Uno program"""
import json, sys
P = {p["id"]: p for p in json.load(open(sys.argv[1]))}
A = {a["id"]: a for a in json.load(open(sys.argv[2]))}
ids = sys.argv[3].split(",")
fam = {"rule": 0, "trend": 1, "noise": 2}
print("#define N_UNO %d" % len(ids))
print("static const uno_problem UNO_PROBLEMS[N_UNO] PROGMEM = {")
for i in ids:
    p = P[i]; e = p["examples"]; pad = [0] * (12 - len(e))
    print("    { %d, %d, %d, { %s }, { %s }, { %s } }," % (int(i[1:]), fam[A[i]["family"]], len(e), ", ".join(map(str, [a for a, _ in e] + pad)),
          ", ".join(map(str, [b for _, b in e] + pad)), ", ".join(map(str, p["questions"]))))
print("};")

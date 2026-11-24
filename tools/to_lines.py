"""Turns a problems.json into the line format native_solve / wasm_solve read.   python3 to_lines.py problems.json"""
import json, sys
for p in json.load(open(sys.argv[1])):
    if p["type"] == "sequence": print("S", len(p["terms"]), *p["terms"])
    else:
        e = p["examples"]; print("R", len(e), *[a for a, _ in e], *[b for _, b in e], len(p["questions"]), *p["questions"])

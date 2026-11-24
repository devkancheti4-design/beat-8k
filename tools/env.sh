# env.sh -- sourced by the scripts: where this repository is, and where edai's challenge is.
# The challenge is edai's repository (github.com/devkancheti4-design/edai) checked out at commit b3c00e9: its
# problems, keys, score.py, compiled library and runner. Default: a folder named edai-challenge beside this one;
# set EDAI to use another.
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CH=${EDAI:-$ROOT/../edai-challenge}
SETS="main seed-23 seed-24 seed-25 seed-26 seed-27"
[ -f "$CH/score.py" ] || { echo "edai's challenge not found at $CH (check out edai at b3c00e9 there, or set EDAI)"; exit 1; }
setdir() { if [ "$1" = main ]; then echo "$CH/problems"; else echo "$CH/problems/fresh/$1"; fi; }   # a set's folder

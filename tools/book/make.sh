#!/usr/bin/env bash
# Rebuild the whole BOOK-1 content pack.
#   tools/book/make.sh PACK_DIR
# PACK_DIR's parent holds src/Gastro.pdf (not in git), and receives src/words.json, render/ and build/ (generated).
# In the repo: put the PDF at content/src/Gastro.pdf and run  tools/book/make.sh content/gastro
set -euo pipefail
T=$(cd "$(dirname "$0")" && pwd)
PACK=$(cd "${1:?usage: make.sh PACK_DIR}" && pwd)
R=$(dirname "$PACK")
cd "$R"
mkdir -p build render src
[[ -f src/Gastro.pdf ]] || { echo "missing $R/src/Gastro.pdf" >&2; exit 1; }
if [[ ! -f src/words.json ]]; then
  pdftotext -bbox-layout src/Gastro.pdf src/bbox.html
  [[ -f render/p-01.png ]] || pdftoppm -r 150 -png src/Gastro.pdf render/p
  python3 -I "$T/pdfwords.py" src/bbox.html src/Gastro.pdf render src/words.json
fi
python3 -I "$T/build.py" "$PACK"
cp build/fixes_merged.json "$PACK/fixes.json"
python3 -I "$T/atoms.py" "$PACK"
python3 -I "$T/numbers.py" "$PACK"
python3 -I "$T/figs.py" "$PACK"
python3 -I "$T/figlabels.py" "$PACK"     # label boxes (+ fig/label_positions_manual.json)
[[ -f src/gastro-quests.json ]] || cp "$PACK/gastro-quests.json" src/gastro-quests.json
[[ -f src/institute.map ]] || cp "$PACK/institute.map" src/institute.map
python3 -I "$T/quests.py" "$PACK"
python3 -I "$T/errata.py" "$PACK"
if ls "$T"/topics/*.py >/dev/null 2>&1; then python3 -I "$T/compile.py" "$PACK"; fi
python3 -I "$T/report.py" "$PACK"
python3 -I "$T/leakguard.py" build "$PACK"
python3 -I "$PACK/check.py" "$PACK" --topics "$(python3 -I -c "import json,sys;print(','.join(sorted({json.loads(l)['topic'] for l in open(sys.argv[1])})))" "$PACK/facts.jsonl")" || true

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
if ls "$T"/topics/*.py >/dev/null 2>&1; then
  python3 -I "$T/compile.py" "$PACK"
  python3 -I "$T/packbin.py" "$PACK" "$PACK/gastro.mgpack"
fi
python3 -I "$T/inflections.py" "$PACK"      # §2: answers not verbatim (report appendix)
python3 -I "$T/typofacts.py" "$PACK"        # §1: facts whose only testable token is a misprint
if [[ -f src/institute_model_page.html ]]; then   # the "Gastro Institute 3D" page (MAP-2): its furniture, the plans, the book inside it
  python3 -I -c "import sys;s=open(sys.argv[1],encoding='utf-8').read();i=s.index('<script>var ROOMS=');j=s.index('</script>',i);open(sys.argv[2],'w',encoding='utf-8').write(s[i+8:j]+'\nif(typeof module!==\"undefined\"){module.exports.ROOMS=ROOMS;module.exports.loc=loc;module.exports.FH=FH;}\n')" src/institute_model_page.html src/institute_model.js
  node "$T/model_occupancy.js" src/institute_model.js "$PACK/model_occupancy.json"
fi
python3 -I "$T/plans.py" "$PACK"
cp "$T/wordrule.py" "$PACK/wordrule.py"
python3 -I "$T/report.py" "$PACK"
python3 -I "$T/leakguard.py" build "$PACK"
python3 -I "$PACK/check.py" "$PACK" || true      # whole book: ledgers, map placement, script length, binary pack
python3 -I "$T/review_sheets.py" "$PACK"    # §5: review_errata.csv, review_hidden.csv (decisions already filled are kept)
python3 -I "$T/engine_prompt.py" "$PACK" "$PACK/gastro.mgpack"
if [[ -f src/institute_model_page.html ]]; then python3 -I "$T/model_book.py" "$PACK" src/institute_model_page.html build/model_artifact; fi

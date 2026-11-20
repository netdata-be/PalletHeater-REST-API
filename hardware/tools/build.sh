#!/bin/sh
# Rebuild the board: schematic -> ERC -> netlist -> placement -> Freerouting -> zones -> DRC.
#   FREEROUTING=/path/to/freerouting.jar hardware/tools/build.sh
# Needs KiCad 10 (macOS app bundle paths below; override with KICAD_APP) and Java.
set -e
cd "$(dirname "$0")/.."
APP=${KICAD_APP:-/Applications/KiCad/KiCad.app}
CLI=$APP/Contents/MacOS/kicad-cli
PY=$APP/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
FR=${FREEROUTING:?set FREEROUTING to the freerouting jar}
P=openpelletbridge/openpelletbridge

python3 tools/gen_schematic.py
"$CLI" sch erc --severity-all -o openpelletbridge/erc.rpt $P.kicad_sch | grep Found
"$CLI" sch export netlist -o $P.net $P.kicad_sch >/dev/null
"$CLI" sch export pdf -o $P.pdf $P.kicad_sch >/dev/null
"$CLI" sch export bom --fields 'Reference,Value,Footprint,MPN,Note,${QUANTITY}' --group-by Value,Footprint,MPN \
    --labels 'Refs,Value,Footprint,MPN,Note,Qty' --exclude-dnp -o openpelletbridge/bom.csv $P.kicad_sch >/dev/null
route() {   # Freerouting: $1 = label
    LOG=$(java -Djava.awt.headless=true -jar "$FR" --gui.enabled=false -de $P.dsn -do $P.ses -mp 60 2>&1 \
          | grep "Auto-routing stage completed" | sed 's/^.*final score/final score/' | cut -c1-60)
    echo "  $1: $LOG"
}
pyq() { "$PY" "$@" 2>&1 | grep -v wxApp | grep -v 'Debug:' | grep -v '^$' | grep -v '^   [UJ]'; }
# Freerouting is chaotic: a small change can leave a few connections open. Try the
# strategies in turn until DRC finds no unconnected item.
for STRAT in all signals-first planes-first; do
    pyq tools/gen_pcb.py place $STRAT
    route "routing ($STRAT)"
    if [ $STRAT != all ]; then
        pyq tools/gen_pcb.py pass2 $P.ses
        route "routing pass 2"
    fi
    pyq tools/gen_pcb.py finish $P.ses
    OUT=$("$CLI" pcb drc --severity-all --schematic-parity -o openpelletbridge/drc.rpt $P.kicad_pcb | grep Found)
    echo "$OUT" | sed 's/^/  /'
    case "$OUT" in *"Found 0 unconnected"*) break;; esac
done

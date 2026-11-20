#!/bin/sh
# Production files for a PCB maker / assembler, from the current .kicad_pcb:
#   hardware/tools/export_production.sh
# Output in hardware/openpelletbridge/production/:
#   openpelletbridge-gerbers.zip   Gerbers + Excellon drill files (upload this to the maker)
#   drill-map.pdf                  drill map
#   positions.csv                  pick and place (KiCad format)
#   cpl-jlcpcb.csv, bom-jlcpcb.csv pick and place / BOM in the JLCPCB assembly format
#   bom-mpn.csv                    BOM by manufacturer part number (Aisler, or anyone buying at
#                                  Mouser / DigiKey / Farnell); use with positions.csv
#   assembly-top.pdf               assembly drawing (fab layer, references, outline)
#   openpelletbridge.step          3D model of the assembled board (enclosure design)
set -e
cd "$(dirname "$0")/.."
APP=${KICAD_APP:-/Applications/KiCad/KiCad.app}
CLI=$APP/Contents/MacOS/kicad-cli
PCB=openpelletbridge/openpelletbridge.kicad_pcb
SCH=openpelletbridge/openpelletbridge.kicad_sch
OUT=openpelletbridge/production
rm -rf $OUT && mkdir -p $OUT/gerbers

"$CLI" pcb export gerbers -o $OUT/gerbers/ \
    -l F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts \
    --subtract-soldermask $PCB >/dev/null
"$CLI" pcb export drill -o $OUT/gerbers/ --format excellon --excellon-units mm \
    --excellon-separate-th --generate-map --map-format pdf $PCB >/dev/null
mv $OUT/gerbers/*-drl_map.pdf $OUT/ 2>/dev/null || true
for f in $OUT/*-drl_map.pdf; do [ -f "$f" ] && mv "$f" "$OUT/drill-map-$(basename "$f" | sed 's/.*-\(N*PTH\)-drl_map.pdf/\1/').pdf"; done
(cd $OUT/gerbers && zip -q ../openpelletbridge-gerbers.zip *)

"$CLI" pcb export pos -o $OUT/positions.csv --format csv --units mm --side both --exclude-dnp $PCB >/dev/null
"$CLI" pcb export pdf -o $OUT/assembly-top.pdf -l F.Fab,Edge.Cuts --cl F.Fab \
    --mode-single --include-border-title $PCB >/dev/null 2>&1 || \
"$CLI" pcb export pdf -o $OUT/assembly-top.pdf -l F.Fab,Edge.Cuts $PCB >/dev/null
"$CLI" pcb export step -o $OUT/openpelletbridge.step --subst-models --force $PCB >/dev/null 2>&1 || \
    echo "  (STEP export failed - skipped)"
"$CLI" sch export bom -o $OUT/bom.csv --fields 'Reference,Value,Footprint,Manufacturer,MPN,LCSC,Assembly,${QUANTITY}' \
    --group-by Value,Footprint,Manufacturer,MPN,LCSC --labels 'Refs,Value,Footprint,Manufacturer,MPN,LCSC,Assembly,Qty' --exclude-dnp $SCH >/dev/null

# JLCPCB formats: CPL "Designator,Mid X,Mid Y,Layer,Rotation", BOM "Comment,Designator,Footprint,LCSC"
python3 - "$OUT" <<'EOF'
import csv, sys, os
out = sys.argv[1]
rows = list(csv.DictReader(open(os.path.join(out, "positions.csv"))))
# parts soldered by hand (field Assembly = hand, e.g. the THT stove connector) stay out of
# the assembly files
hand = set()
for r in csv.DictReader(open(os.path.join(out, "bom.csv"))):
    if r.get("Assembly") == "hand":
        hand.update(x.strip() for x in r["Refs"].split(","))
rows = [r for r in rows if r["Ref"] not in hand]
# JLCPCB places its own part models, with their own origin and pin-1 orientation. Corrections
# per footprint (regex on the KiCad footprint name): extra rotation in degrees, and the offset
# (x, y in mm, footprint coordinates, y down) from the KiCad origin to the centre JLCPCB uses.
# Checked in the JLCPCB 3D preview (18/11/2026): the module sat 2.55 mm towards the antenna
# (KiCad's origin is not the pad centre), the USB-C 1.39 mm towards the edge, SOT-23-5 and the
# WS2812B-2020 were turned 180 deg (for the LEDs that swaps VDD and GND), SOT-23-6, TSOT-23-6
# and SC-70-6 90 deg. Check every polarised part in the preview after a footprint change.
import math, re
CPL_FIX = [
    (r"^ESP32-S2-MINI-1$", 0, (0.0, 2.55)),
    (r"^SOT-23-5$", 180, (0.0, 0.0)),                 # U3, U7 (checked 18/11/2026)
    (r"^(SOT-23-6|TSOT-23-6)$", 270, (0.0, 0.0)),     # U1, U2 (checked 18/11/2026)
    (r"^SOT-363_SC-70-6$", 270, (0.0, 0.0)),          # (was U8 / U9 LM66100, checked 18/11/2026; no SC-70 left)
    (r"^LED_WS2812B-2020", 180, (0.0, 0.0)),
    # J1, GCT USB4105 (C3020560): the EasyEDA footprint JLCPCB uses has its origin 1.29 mm
    # further back than KiCad's (signal pads at y -2.37 vs -3.68, pegs -1.33 vs -2.605); without
    # this the part sat 1.29 mm towards the board edge in the preview (19/11/2026)
    (r"^USB_C_Receptacle_GCT_USB4105", 0, (0.0, -1.29)),
]
with open(os.path.join(out, "cpl-jlcpcb.csv"), "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    for r in rows:
        x, y, rot = float(r["PosX"]), float(r["PosY"]), float(r["Rot"])
        for pat, drot, (dx, dy) in CPL_FIX:
            if re.search(pat, r["Package"]):
                a = math.radians(rot)   # positions file: y up, rotation counter-clockwise
                x += dx * math.cos(a) + dy * math.sin(a)
                y += dx * math.sin(a) - dy * math.cos(a)
                rot = (rot + drot) % 360
                break
        w.writerow([r["Ref"], "%.4fmm" % x, "%.4fmm" % y, "Top" if r["Side"] == "top" else "Bottom", "%g" % rot])
bom = [r for r in csv.DictReader(open(os.path.join(out, "bom.csv"))) if r.get("Assembly") != "hand"]
import re
def expand(refs):
    """KiCad groups references as ranges (C10-C12); JLCPCB needs each one: C10,C11,C12"""
    out = []
    for part in refs.split(","):
        m = re.fullmatch(r"([A-Za-z]+)(\d+)-[A-Za-z]*(\d+)", part.strip())
        if m:
            out += [m.group(1) + str(i) for i in range(int(m.group(2)), int(m.group(3)) + 1)]
        elif part.strip():
            out.append(part.strip())
    return out
placed = {r["Ref"] for r in rows}
bom_refs = [x for r in bom for x in expand(r["Refs"])]
missing = placed - set(bom_refs)
extra = set(bom_refs) - placed
assert not missing and not extra, ("BOM / CPL mismatch", sorted(missing), sorted(extra))
# one line per LCSC part: JLCPCB flags two BOM lines matched to the same part (e.g. the
# RESET and BOOT buttons, which have different values in the schematic)
merged = {}
for r in bom:
    assert r["LCSC"], ("no LCSC number", r["Refs"])
    m = merged.setdefault(r["LCSC"], {"values": [], "refs": [], "fp": r["Footprint"].split(":")[-1]})
    if r["Value"] not in m["values"]:
        m["values"].append(r["Value"])
    m["refs"] += expand(r["Refs"])
with open(os.path.join(out, "bom-jlcpcb.csv"), "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Comment", "Designator", "Footprint", "LCSC"])
    for lcsc, m in merged.items():
        w.writerow([" / ".join(m["values"]), ",".join(m["refs"]), m["fp"], lcsc])
# BOM by manufacturer part number, for assemblers that buy at Mouser / DigiKey / Farnell (Aisler)
mpn = {}
for r in bom:
    k = (r["Manufacturer"], r["MPN"])
    m = mpn.setdefault(k, {"refs": [], "values": [], "fp": r["Footprint"].split(":")[-1]})
    m["refs"] += expand(r["Refs"])
    if r["Value"] not in m["values"]:
        m["values"].append(r["Value"])
with open(os.path.join(out, "bom-mpn.csv"), "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Designator", "Quantity", "Manufacturer", "MPN", "Value", "Footprint"])
    for (man, pn), m in mpn.items():
        assert pn, ("no MPN", m["refs"])
        w.writerow([",".join(m["refs"]), len(m["refs"]), man, pn, " / ".join(m["values"]), m["fp"]])
print("  %d placements, %d BOM lines (%d JLCPCB lines); every placed part is in the BOM with an LCSC number; by hand: %s" % (len(rows), len(bom), len(merged), ", ".join(sorted(hand)) or "-"))
EOF
ls -1 $OUT | sed 's/^/  /'

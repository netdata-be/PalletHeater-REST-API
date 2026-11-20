#!/usr/bin/env python3
"""Build hardware/openpelletbridge/openpelletbridge.kicad_pcb from the schematic netlist.

Run with KiCad's own Python (it has the pcbnew module):

    PY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
    $PY hardware/tools/gen_pcb.py place              # board, placement, rules -> .kicad_pcb + .dsn
    java -jar freerouting.jar -de <.dsn> -do <.ses>   # autoroute
    $PY hardware/tools/gen_pcb.py finish <.ses>       # import routes, zones, fill

The footprints keep the schematic's symbol UUIDs, so "Update PCB from Schematic"
in KiCad works on the result.
"""
import math, os, sys
import pcbnew
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kisym

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.join(HERE, "..", "openpelletbridge")
NET = os.path.join(PRJ, "openpelletbridge.net")
PCB = os.path.join(PRJ, "openpelletbridge.kicad_pcb")
DSN = os.path.join(PRJ, "openpelletbridge.dsn")
FPDIR = os.path.join(kisym.KICAD, "footprints")

# Board size (mm) and the page offset of its top-left corner
W, H = 40.0, 57.0
OX, OY = 100.0, 100.0

def mm(v):
    return pcbnew.FromMM(v)

def pt(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))

# ── Placement: ref -> (x, y, rotation) in board mm, top side ─────────────────
# Top: module with the antenna at the top edge. Bottom edge: stove connector J2
# (left) and USB-C J1 (right). Left of the module: boot button, 3.3 V LDO.
# Right of the module: reset button, LEDs. Band below the module: optocouplers
# (stove side towards J2), the stove-side parts, the 20 V step-down and USB.
PLACE = {
    "U4":  (20, 9.8, 0),
    # left column: BOOT button (label below it), IO0 pull-up, 3.3 V LDO and its capacitors
    "SW2": (6.2, 7.6, 0),
    "R4":  (7, 12.2, 0),
    "C2":  (5.4, 15, 90),
    "C3":  (11.0, 8.1, 90),       # 100 nF right next to the module's 3V3 pin 3 (Espressif)
    "C15": (11.05, 11.4, 90),      # 10 uF bulk at the module; C2 stays at the LDO
    "U3":  (6.6, 18.6, 0),
    "C1":  (10.8, 18.4, 90),
    # right column: RESET button (label below it), EN RC, LED driver, then the LEDs SYS and
    # STOVE (the TX / RX LEDs sit above the optocouplers, see below)
    "SW1": (32.7, 7.6, 0),
    "R3":  (30.4, 12.4, 0),
    "C9":  (34, 12.4, 0),
    "U7":  (31, 15.15, 0),
    "C10": (35.2, 15.15, 90),
    "R8":  (31.7, 17.9, 0),
    "R9":  (34.9, 17.9, 0),
    "LED1": (31.9, 20.1, 0),
    "LED2": (35.1, 20.1, 0),
    "C11": (31.9, 23.5, 0),
    "C12": (35.1, 23.5, 0),
    # band below the module, ESP side (y < 36)
    "R16": (10.6, 14.8, 90),     # pull-up of the mux status (IO4: power source)
    "R12": (4.9, 22.2, 0),
    "R5":  (8.1, 22.2, 0),
    "R7":  (11.3, 22.2, 0),
    # optocouplers across the isolation line (y = 36)
    "U5":  (7.4, 36, 270),
    "U6":  (13.8, 36, 90),
    # isolation jumpers straddle the line
    "JP1": (18.2, 36, 90),
    "JP2": (20.8, 36, 90),
    # stove side (below the line), next to J2
    "F2":  (5.6, 43.6, 0),
    "D3":  (10.6, 43.6, 0),
    "D4":  (15.2, 43.6, 0),
    "R6":  (18.6, 41.2, 90),
    "J2":  (12.65, 48.6, 0),
    # 20 V -> 5 V step-down (ESP side, right of the isolation strip)
    "C4":  (25, 34.2, 90),
    "C5":  (27.2, 34.4, 90),
    "U2":  (30.6, 34.2, 0),
    "C6":  (34.6, 34, 90),
    "L1":  (31.1, 38.8, 0),
    "C7":  (25.1, 39, 90),
    "C8":  (27.4, 39, 90),
    # power mux (stove first, USB fallback) and its PR1 divider, at the right edge
    "U8":  (35, 41.2, 90),
    "R14": (37.7, 39.6, 90),
    "R15": (37.7, 42.8, 90),
    # USB-C, ESD, CC resistors, fuse and the mux input capacitor
    "U1":  (30, 44.4, 0),
    "F1":  (25.6, 43.4, 0),
    "C14": (25.6, 46.4, 0),
    "R1":  (33.8, 45.6, 90),
    "R2":  (36, 45.6, 90),
    "J1":  (29.5, 53.35, 0),
    # buzzer band below the module (ESP side)
    "BZ1": (20, 28.6, 0),
    "Q1":  (32.45, 27.6, 0),
    "R13": (29.6, 28.0, 90),
    # serial activity LEDs just above the isolation line, each above its optocoupler: red TX
    # above U5, green RX above U6 (ESP side: they are on 3V3 and STOVE_TX / STOVE_RX)
    "D5":  (7.4, 30.3, 0),
    "D6":  (13.8, 30.3, 0),
    "R10": (7.4, 27.4, 0),
    "R11": (13.8, 27.4, 0),
    "D7":  (26.4, 27.4, 0),
    "C13": (26.4, 30.0, 0),
    # reverse polarity diode, left of U5 on the stove side
    "D8":  (3.0, 39.0, 90),
    # M2 mounting holes: two at the sides of the buzzer band, two in the bottom corners. Not in
    # the top corners: that is the antenna keep-out of the module (no metal within 15 mm)
    "MH1": (3.0, 28.0, 0), "MH2": (W - 3.0, 28.0, 0), "MH3": (2.4, H - 2.4, 0), "MH4": (W - 2.4, H - 2.4, 0),
    # test points: pads on the bottom (BOTTOM); ESP side under the module, stove side
    # in the GND_S area above the stove connector
    "TP1": (14.0, 16.5, 0), "TP2": (17.0, 16.5, 0), "TP3": (20.0, 16.5, 0), "TP4": (23.0, 16.5, 0), "TP5": (26.0, 16.5, 0),
    "TP6": (4.0, 44.6, 0), "TP7": (8.0, 44.6, 0), "TP8": (12.0, 44.6, 0), "TP9": (16.0, 44.6, 0),
}

# Net classes: power nets wider
POWER_NETS = ["GND", "+5V", "+3V3", "/VBUS", "/VBUS_F", "/VIN_BUCK", "/+5V_BUCK", "/SW", "/+20V_S", "/+20V_F", "/+20V_P", "/GND_S"]

# parts on the bottom side: only the test pads (nothing is assembled there)
BOTTOM = {"TP%d" % i for i in range(1, 10)}


def load_netlist():
    t = kisym.parse(open(NET).read())[0]
    comps = []
    for c in kisym.find(kisym.first(t, "components"), "comp"):
        comps.append({
            "ref": kisym.first(c, "ref")[1],
            "value": kisym.first(c, "value")[1],
            "footprint": kisym.first(c, "footprint")[1],
            "uuid": kisym.first(c, "tstamps")[1],
            "datasheet": (kisym.first(c, "datasheet") or [None, ""])[1],
            "fields": {kisym.first(f, "name")[1]: (f[2] if len(f) > 2 else "")
                       for f in kisym.find(kisym.first(c, "fields") or [], "field")},
            "in_bom": not any(kisym.first(p, "name")[1] == "exclude_from_bom" for p in kisym.find(c, "property")),
        })
    nets = {}
    for n in kisym.find(kisym.first(t, "nets"), "net"):
        name = kisym.first(n, "name")[1]
        for node in kisym.find(n, "node"):
            nets.setdefault(kisym.first(node, "ref")[1], {})[kisym.first(node, "pin")[1]] = name
    return comps, nets


def edge(board):
    r = 1.0   # corner radius
    def seg(a, b):
        s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(*a)); s.SetEnd(pt(*b)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1)); board.Add(s)
    def arc(c, start, mid, end):
        s = pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_ARC)
        s.SetArcGeometry(pt(*start), pt(*mid), pt(*end)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1)); board.Add(s)
    k = r * (1 - 0.7071068)
    seg((r, 0), (W - r, 0)); seg((W, r), (W, H - r)); seg((W - r, H), (r, H)); seg((0, H - r), (0, r))
    arc(None, (W - r, 0), (W - k, k), (W, r))
    arc(None, (W, H - r), (W - k, H - k), (W - r, H))
    arc(None, (r, H), (k, H - k), (0, H - r))
    arc(None, (0, r), (k, k), (r, 0))


def rules(board):
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_MinClearance = mm(0.15)
    ds.m_TrackMinWidth = mm(0.15)
    ds.m_ViasMinSize = mm(0.5)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(0.3)
    ns = ds.m_NetSettings
    d = ns.GetDefaultNetclass()
    d.SetClearance(mm(0.2)); d.SetTrackWidth(mm(0.2)); d.SetViaDiameter(mm(0.6)); d.SetViaDrill(mm(0.3))
    p = pcbnew.NETCLASS("Power")
    p.SetClearance(mm(0.2)); p.SetTrackWidth(mm(0.4)); p.SetViaDiameter(mm(0.6)); p.SetViaDrill(mm(0.3))
    ns.SetNetclass("Power", p)
    for n in POWER_NETS:
        ns.SetNetclassPatternAssignment(n, "Power")
    ns.RecomputeEffectiveNetclasses()


def rule_area(board, name, poly, layers=(pcbnew.F_Cu, pcbnew.B_Cu), tracks=True, vias=True, pour=True, fps=False):
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True); z.SetZoneName(name)
    ls = pcbnew.LSET()
    for l in layers:
        ls.AddLayer(l)
    z.SetLayerSet(ls)
    z.SetDoNotAllowTracks(tracks); z.SetDoNotAllowVias(vias); z.SetDoNotAllowZoneFills(pour)
    z.SetDoNotAllowPads(False); z.SetDoNotAllowFootprints(fps)
    o = z.Outline(); o.NewOutline()
    for x, y in poly:
        o.Append(mm(OX + x), mm(OY + y))
    board.Add(z)
    return z


PRO = os.path.join(PRJ, "openpelletbridge.kicad_pro")

# 3D models the KiCad library does not have (hardware/openpelletbridge/3d): file, offset (x, y, z mm,
# y up), rotation (z degrees). Sources: Espressif kicad-libraries (module), LCSC / EasyEDA (USB-C
# C165948, WS2812B-2020 C965555); D5 uses KiCad's 0805 LED model (same 2.0 x 1.25 mm body).
MODELS = {
    "U4":   ("${KIPRJMOD}/3d/ESP32-S3-MINI-1.step", (-7.7, -10.25, 0), 0),
    "LED1": ("${KIPRJMOD}/3d/WS2812B-2020.step", (0, 0, 0), 0),
    "LED2": ("${KIPRJMOD}/3d/WS2812B-2020.step", (0, 0, 0), 0),
    "J2":   ("${KIPRJMOD}/3d/KF2EDGR-3.5-4P.step", (0, -3.3, 0), 0),  # LCSC / EasyEDA C441173; body -1.3..7.9 mm from the pins as in its footprint
    "SW1":  ("${KIPRJMOD}/3d/TS-1187A.step", (0, 0, 0), 0),         # LCSC / EasyEDA C318884
    "SW2":  ("${KIPRJMOD}/3d/TS-1187A.step", (0, 0, 0), 0),
    "BZ1":  ("${KIPRJMOD}/3d/MLT-5020.step", (0, 0, 0), 90),       # LCSC / EasyEDA C94598
}

def set_models(board):
    for ref, (path, off, rot) in MODELS.items():
        fp = board.FindFootprintByReference(ref)
        fp.Models().clear()
        m = pcbnew.FP_3DMODEL()
        m.m_Filename = path
        m.m_Offset = pcbnew.VECTOR3D(*off)
        m.m_Rotation = pcbnew.VECTOR3D(0, 0, rot)
        m.m_Scale = pcbnew.VECTOR3D(1, 1, 1)
        fp.Models().push_back(m)


def write_project():
    """Design rules and net classes live in the project file: KiCad (and the DSN
    export) take them from there."""
    import json
    pro = json.load(open(PRO)) if os.path.exists(PRO) else {}
    pro.setdefault("meta", {"filename": "openpelletbridge.kicad_pro", "version": 3})
    pro.setdefault("board", {}).setdefault("design_settings", {})["rules"] = {
        "min_clearance": 0.15, "min_track_width": 0.15, "min_copper_edge_clearance": 0.3,
        "min_via_diameter": 0.5, "min_via_annular_width": 0.1, "min_through_hole_diameter": 0.3,
        "min_hole_to_hole": 0.25, "min_hole_clearance": 0.15,   # GCT USB4105 land pattern: 0.19 mm from its own plastic pegs (NPTH)
        "min_connection": 0.0,
        "min_resolved_spokes": 2, "min_silk_clearance": 0.0, "min_text_height": 0.6,
        "min_text_thickness": 0.1, "max_error": 0.005, "allow_microvias": False,
        "allow_blind_buried_vias": False, "solder_mask_to_copper_clearance": 0.0,
        "min_groove_width": 0.0, "min_microvia_diameter": 0.2, "min_microvia_drill": 0.1,
        "use_height_for_length_calcs": True}
    base = {"bus_width": 12, "diff_pair_gap": 0.25, "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2,
            "line_style": 0, "microvia_diameter": 0.3, "microvia_drill": 0.1, "wire_width": 6,
            "pcb_color": "rgba(0, 0, 0, 0.000)", "schematic_color": "rgba(0, 0, 0, 0.000)"}
    pro["net_settings"] = {
        "classes": [dict(base, name="Default", clearance=0.2, track_width=0.2, via_diameter=0.6, via_drill=0.3, priority=2147483647),
                    dict(base, name="Power", clearance=0.2, track_width=0.4, via_diameter=0.6, via_drill=0.3, priority=0)],
        "meta": {"version": 4}, "net_colors": None, "netclass_assignments": None,
        "netclass_patterns": [{"netclass": "Power", "pattern": n} for n in POWER_NETS]}
    json.dump(pro, open(PRO, "w"), indent=2)


# Connected by the copper planes and stitching vias, not routed as tracks
PLANE_NETS = ["GND", "/GND_S"]

def strip_nets(dsn, names):
    """Leave plane nets out of the autorouter's job: drop their (net ...) blocks and
    their names from the (class ...) lists of the Specctra file. Done on the text:
    the Specctra syntax (string_quote ") does not survive an S-expression round trip."""
    import re
    t = open(dsn).read()
    for n in names:
        while True:
            m = re.search(r"\(net " + re.escape(n) + r"\s", t)
            if not m:
                break
            depth, k = 0, m.start()
            while True:
                if t[k] == "(":
                    depth += 1
                elif t[k] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                k += 1
            t = t[:m.start()] + t[k + 1:]
    def fix_class(m):
        toks = [x for x in m.group(2).split() if x not in names]
        return "(class " + m.group(1) + " " + " ".join(toks) + "\n"
    t = re.sub(r"\(class (\S+)([^()]*)", fix_class, t)
    open(dsn, "w").write(t)


def place():
    write_project()
    comps, pinnets = load_netlist()
    board = pcbnew.BOARD()
    rules(board)
    nets = {}
    def net(name):
        if name not in nets:
            n = pcbnew.NETINFO_ITEM(board, name); board.Add(n); nets[name] = n
        return nets[name]
    for c in comps:
        lib, name = c["footprint"].split(":")
        libdir = os.path.join(PRJ, lib + ".pretty") if lib == "openpelletbridge" else os.path.join(FPDIR, lib + ".pretty")
        fp = pcbnew.FootprintLoad(libdir, name)
        assert fp, c["footprint"]
        fp.SetReference(c["ref"]); fp.SetValue(c["value"])
        fp.SetPath(pcbnew.KIID_PATH("/" + c["uuid"]))
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        for k, v in c["fields"].items():
            if k in ("Manufacturer", "MPN", "Note", "LCSC", "Assembly"):
                fp.SetField(k, v)
                fp.GetField(k).SetVisible(False)
        fp.SetExcludedFromBOM(not c["in_bom"])
        if c["datasheet"] and c["datasheet"] != "~":
            fp.SetField("Datasheet", c["datasheet"])
            fp.GetField("Datasheet").SetVisible(False)
        board.Add(fp)
        x, y, r = PLACE[c["ref"]]
        fp.SetPosition(pt(x, y)); fp.SetOrientationDegrees(r)
        if c["ref"] in BOTTOM:
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        if c["ref"].startswith(("TP", "MH")):
            # bare pads and holes: not in the BOM or the pick-and-place files
            fp.SetExcludedFromPosFiles(True); fp.SetExcludedFromBOM(True)
            fp.Reference().SetVisible(False)
        for pad in fp.Pads():
            n = pinnets.get(c["ref"], {}).get(pad.GetNumber())
            if n:
                pad.SetNet(net(n))
        # keep reference text small on the silkscreen
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        fp.Reference().SetTextThickness(mm(0.12))
        fp.Value().SetVisible(False)
        # no references on the silkscreen: small parts have no room for them, and the main
        # parts get a description instead (silk()); the assembly drawing has the references
        if True:
            fp.Reference().SetVisible(False)
    set_models(board)
    missing = set(PLACE) - {c["ref"] for c in comps}
    assert not missing, missing
    edge(board)
    # isolation: no tracks, vias or pour of either side across the line between the stove part
    # (bottom left) and the rest; only the jumpers JP1 / JP2 cross it
    # 0.5 mm along the board edge: Freerouting does not know the edge clearance
    e = 0.5
    for name, (x0, y0, x1, y1) in {"Edge-T": (0, 0, W, e), "Edge-B": (0, H - e, W, H),
                                    "Edge-L": (0, 0, e, H), "Edge-R": (W - e, 0, W, H)}.items():
        rule_area(board, name, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], pour=False)
    silk(board)
    for name, (x0, y0, x1, y1) in {"Isolation-1": (0, 35.6, 17.0, 36.4), "Isolation-2": (19.3, 35.6, 19.7, 36.4),
                                    "Isolation-3": (22.0, 35.6, 23.2, 36.4), "Isolation-4": (22.6, 36.4, 23.2, H)}.items():
        rule_area(board, name, [(x0, y0), (x1, y0), (x1, y1), (x0, y1)])
    # no copper around the mounting holes (both layers): a screw or metal standoff touches
    # nothing, so the stove side stays isolated whatever the board is mounted on
    for ref in ("MH1", "MH2", "MH3", "MH4"):
        cx, cy, _ = PLACE[ref]
        rule_area(board, "Hole-" + ref, [(cx + 2.4 * math.cos(a * math.pi / 8), cy + 2.4 * math.sin(a * math.pi / 8)) for a in range(16)])
    return board


def silk(board):
    """Labels on the silkscreen: connector pins, buttons, LEDs, jumpers, name."""
    def text(t, x, y, size=0.8, angle=0, layer=pcbnew.F_SilkS):
        s = pcbnew.PCB_TEXT(board)
        s.SetText(t); s.SetPosition(pt(x, y)); s.SetLayer(layer)
        s.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); s.SetTextThickness(mm(max(0.1, size * 0.15)))
        s.SetTextAngleDegrees(angle)
        board.Add(s)
    x2, y2, _ = PLACE["J2"]
    for i, t in enumerate(["GND", "5V", "20V", "DATA"]):
        text(t, x2 - 5.25 + 3.5 * i, y2 - 3.1, 0.7)   # J2 origin: centre of the pin row
    # under the buttons: above them the switch body would cover the text
    text("RESET", PLACE["SW1"][0], PLACE["SW1"][1] + 3.4, 0.7)
    text("BOOT", PLACE["SW2"][0], PLACE["SW2"][1] + 3.4, 0.7)
    for ref, t, dx, dy in (("D5", "TX", 2.4, 0.3), ("D6", "RX", 2.4, 0.3), ("LED1", "SYS", 0, 1.9), ("LED2", "STOVE", -0.4, 1.9)):
        text(t, PLACE[ref][0] + dx, PLACE[ref][1] + dy, 0.6)
    text("STOVE PWR", 19.5, 38.2, 0.7)
    # descriptions instead of references
    text("3V3 LDO", 3.6, PLACE["U3"][1], 0.6, 90)
    text("5V BUCK", PLACE["U2"][0], 31.8, 0.6)
    text("BUZZER", PLACE["BZ1"][0], 25.2, 0.6)
    text("OPTO TX", PLACE["U5"][0], 41.5, 0.6)
    text("OPTO RX", PLACE["U6"][0], 41.5, 0.6)
    text("USB-C", PLACE["J1"][0], 47.8, 0.7)
    # JP1 closes the stove's +20 V, JP2 its ground: marked + and - instead of the references
    text("+", PLACE["JP1"][0], 33.6, 1.0)
    text("-", PLACE["JP2"][0], 33.6, 1.0)
    back_silk(board)


# input range of the stove supply: the buck needs ~7 V for 5 V out, D3 (SMF24CA) starts to
# conduct above 24 V, and F2 (200 mA hold) is sized for >= 12 V (below that the WiFi peaks
# come close to its hold current)
BACK_TEXT = [
    # (text, y, size)
    ("Micronova serial port board", 2.1, 1.0),          # antenna strip: no copper on either side
    ("OpenPelletBridge v0.1", 39.4, 1.0),
    ("Power: 12-24 V DC (stove) or USB-C 5 V", 41.2, 0.7),
]


# labels of the test pads (bottom side), next to the pad
TP_LABELS = {"TP1": "3V3", "TP2": "5V", "TP3": "GND", "TP4": "TX", "TP5": "RX",
             "TP6": "20V", "TP7": "5V_S", "TP8": "DATA", "TP9": "GND_S"}


def back_silk(board):
    """Text on the bottom silkscreen (mirrored, so it reads from the back)."""
    def text(txt, x, y, size):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(txt); t.SetPosition(pt(x, y)); t.SetLayer(pcbnew.B_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(max(0.1, size * 0.15)))
        t.SetMirrored(True)
        board.Add(t)
    for txt, y, size in BACK_TEXT:
        text(txt, W / 2, y, size)
    for ref, txt in TP_LABELS.items():
        x, y, _ = PLACE[ref]
        text(txt, x, y + 1.6, 0.6)


def check(board):
    """Courtyard overlaps and parts outside the board."""
    fps = list(board.GetFootprints())
    boxes = {}
    for f in fps:
        c = f.GetCourtyard(pcbnew.F_CrtYd)
        if c.OutlineCount():
            boxes[f.GetReference()] = c
    probs = []
    refs = sorted(boxes)
    for i, a in enumerate(refs):
        for b in refs[i + 1:]:
            if a == "U4" or b == "U4":
                continue   # the module courtyard includes the antenna keep-out; checked by DRC
            if boxes[a].Collide(boxes[b]):
                probs.append(("overlap", a, b))
    for f in fps:
        bb = f.GetBoundingBox(False)
        for p in f.Pads():
            q = p.GetPosition()
            x, y = pcbnew.ToMM(q.x) - OX, pcbnew.ToMM(q.y) - OY
            if not (0.3 < x < W - 0.3 and 0.3 < y < H - 0.3):
                probs.append(("pad outside", f.GetReference(), p.GetNumber(), round(x, 2), round(y, 2)))
                break
    return probs


def copper_zone(board, netname, poly, priority, layers=(pcbnew.F_Cu, pcbnew.B_Cu)):
    for layer in layers:
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(board.FindNet(netname))
        z.SetAssignedPriority(priority)
        z.SetLocalClearance(mm(0.3))
        z.SetMinThickness(mm(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        z.SetThermalReliefGap(mm(0.3))
        z.SetThermalReliefSpokeWidth(mm(0.4))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_NEVER)   # first fill keeps islands: they get stitching vias
        o = z.Outline(); o.NewOutline()
        for x, y in poly:
            o.Append(mm(OX + x), mm(OY + y))
        board.Add(z)


def stitch(board, netname, pitch=2.5, size=0.6, drill=0.3):
    """Vias between the top and bottom plane of a net, on a grid, wherever both
    planes are filled with room around the via."""
    zones = {z.GetLayer(): z for z in board.Zones() if z.GetNetname() == netname and not z.GetIsRuleArea()}
    if pcbnew.F_Cu not in zones or pcbnew.B_Cu not in zones:
        return
    net = board.FindNet(netname)
    holes = [t.GetPosition() for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
    holes += [p.GetPosition() for f in board.GetFootprints() for p in f.Pads() if p.HasHole()]
    def free(q):
        return all((q.x - h.x) ** 2 + (q.y - h.y) ** 2 > mm(1.2) ** 2 for h in holes)
    r = size / 2 + 0.35   # every point of this circle inside both fills: room for the via
    n = 0
    y = 1.5
    while y < H - 1:
        x = 1.5
        while x < W - 1:
            p = pt(x, y)
            ring = [p] + [pt(x + r * math.cos(a), y + r * math.sin(a)) for a in [i * math.pi / 6 for i in range(12)]]
            if free(p) and all(zones[l].HitTestFilledArea(l, q, 0) for l in (pcbnew.F_Cu, pcbnew.B_Cu) for q in ring):
                v = pcbnew.PCB_VIA(board)
                v.SetPosition(p); v.SetWidth(mm(size)); v.SetDrill(mm(drill)); v.SetNet(net)
                v.SetViaType(pcbnew.VIATYPE_THROUGH)
                board.Add(v); n += 1; holes.append(p)
            x += pitch
        y += pitch
    # every filled fragment of either layer needs at least one via: look for one that
    # has none and put a via at the first spot inside it with room on both layers
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        other = pcbnew.B_Cu if layer == pcbnew.F_Cu else pcbnew.F_Cu
        polys = zones[layer].GetFilledPolysList(layer)
        for i in range(polys.OutlineCount()):
            frag = pcbnew.SHAPE_POLY_SET(); frag.AddOutline(polys.Outline(i))
            vias = [h for h in holes if frag.Contains(h)]
            pads = [p for f in board.GetFootprints() for p in f.Pads()
                    if p.GetNetname() == netname and p.IsOnLayer(layer) and frag.Contains(p.GetPosition())]
            if vias or not pads:
                continue
            bb = frag.BBox()
            done = False
            yy = bb.GetTop()
            while yy < bb.GetBottom() and not done:
                xx = bb.GetLeft()
                while xx < bb.GetRight() and not done:
                    q = pcbnew.VECTOR2I(int(xx), int(yy))
                    ring = [q] + [pcbnew.VECTOR2I(int(xx + mm(r) * math.cos(a)), int(yy + mm(r) * math.sin(a)))
                                  for a in [k * math.pi / 6 for k in range(12)]]
                    if free(q) and all(frag.Contains(c) for c in ring) and \
                            all(zones[other].HitTestFilledArea(other, c, 0) for c in ring):
                        v = pcbnew.PCB_VIA(board)
                        v.SetPosition(q); v.SetWidth(mm(size)); v.SetDrill(mm(drill)); v.SetNet(net)
                        v.SetViaType(pcbnew.VIATYPE_THROUGH)
                        board.Add(v); n += 1; holes.append(q); done = True
                    xx += mm(0.25)
                yy += mm(0.25)
            if not done:
                print("  no room for a via in a", netname, "fragment on", board.GetLayerName(layer), "with", [p.GetParentFootprint().GetReference() + "." + p.GetNumber() for p in pads][:4])
    print("  stitching vias", netname, n)


def finish(ses):
    board = pcbnew.LoadBoard(PCB)
    assert pcbnew.ImportSpecctraSES(board, ses), "SES import failed"
    # ground planes on both layers: GND for the controller, GND_S on the stove side
    # (bottom left, below the isolation line); the isolation strips stay empty
    copper_zone(board, "GND", [(0, 0), (W, 0), (W, H), (0, H)], 0)
    copper_zone(board, "/GND_S", [(0, 36.4), (22.6, 36.4), (22.6, H), (0, H)], 1)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    stitch(board, "GND"); stitch(board, "/GND_S")
    for z in board.Zones():
        if not z.GetIsRuleArea():
            z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(PCB, board)
    tracks = [t for t in board.GetTracks()]
    vias = [t for t in tracks if t.GetClass() == "PCB_VIA"]
    print("saved", os.path.relpath(PCB), "-", len(tracks) - len(vias), "tracks,", len(vias), "vias")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "place"
    if cmd == "place":
        b = place()
        for p in check(b):
            print("  ", *p)
        for ref in ("U5", "U6", "JP1", "JP2"):
            f = b.FindFootprintByReference(ref)
            print("  ", ref, [(p.GetNumber(), p.GetNetname(), round(pcbnew.ToMM(p.GetPosition().y) - OY, 2)) for p in f.Pads()])
        pcbnew.SaveBoard(PCB, b)
        b = pcbnew.LoadBoard(PCB)
        assert pcbnew.ExportSpecctraDSN(b, DSN), "DSN export failed"
        # routing strategy (build.sh tries them in turn): all nets at once, signals
        # first (planes in pass 2) or planes first (signals in pass 2)
        strategy = sys.argv[2] if len(sys.argv) > 2 else "all"
        if strategy == "signals-first":
            strip_nets(DSN, PLANE_NETS)
        elif strategy == "planes-first":
            import re
            nets = set(re.findall(r"\(net (\S+)\s", open(DSN).read()))
            strip_nets(DSN, sorted(nets - set(PLANE_NETS)))
        print("saved", os.path.relpath(PCB), "and", os.path.relpath(DSN), "- strategy", strategy)
    elif cmd == "pass2":
        # signals and supplies routed: load them, export the board again with the
        # plane nets included, so the second routing pass only adds GND / GND_S
        b = pcbnew.LoadBoard(PCB)
        assert pcbnew.ImportSpecctraSES(b, sys.argv[2]), "SES import failed"
        pcbnew.SaveBoard(PCB, b)
        b = pcbnew.LoadBoard(PCB)
        assert pcbnew.ExportSpecctraDSN(b, DSN), "DSN export failed"
        print("pass 1 imported, DSN with the plane nets written")
    elif cmd == "fields":
        # schematic fields changed (LCSC, MPN, notes) but not the circuit: update them
        # on the routed board without placing / routing again
        comps, _ = load_netlist()
        b = pcbnew.LoadBoard(PCB)
        for c in comps:
            fp = b.FindFootprintByReference(c["ref"])
            fp.SetValue(c["value"])
            for k, v in c["fields"].items():
                if k in ("Manufacturer", "MPN", "Note", "LCSC", "Assembly"):
                    fp.SetField(k, v); fp.GetField(k).SetVisible(False)
        pcbnew.SaveBoard(PCB, b); print("fields updated")
    elif cmd == "silk":
        # bottom silkscreen text changed: replace it on the routed board
        b = pcbnew.LoadBoard(PCB)
        for d in list(b.GetDrawings()):
            if d.GetClass() == "PCB_TEXT" and d.GetLayer() == pcbnew.B_SilkS:
                b.Remove(d)
        back_silk(b); pcbnew.SaveBoard(PCB, b); print("bottom silkscreen updated")
    elif cmd == "models":
        b = pcbnew.LoadBoard(PCB); set_models(b); pcbnew.SaveBoard(PCB, b); print("3D models set")
    elif cmd == "finish":
        finish(sys.argv[2])

"""Minimal S-expression reader/writer and KiCad symbol library access.

Used by gen_schematic.py to embed library symbols (flattening 'extends') and to
find pin positions, so the schematic can be generated from a netlist description.
"""
import os, re

KICAD = os.environ.get("KICAD_SHARE", "/Applications/KiCad/KiCad.app/Contents/SharedSupport")


class Sym(str):
    """An unquoted atom."""


def parse(text):
    tok = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))')
    pos, stack, cur = 0, [], []
    while True:
        m = tok.match(text, pos)
        if not m or m.end() == pos:
            break
        pos = m.end()
        if m.group(1):
            stack.append(cur); cur = []
        elif m.group(2):
            done = cur; cur = stack.pop(); cur.append(done)
        elif m.group(3) is not None:
            cur.append(m.group(3).replace('\\"', '"').replace('\\\\', '\\'))
        else:
            cur.append(Sym(m.group(4)))
    return cur


def dump(node, indent=0):
    pad = "\t" * indent
    if isinstance(node, list):
        if not node:
            return "()"
        simple = all(not isinstance(x, list) for x in node)
        if simple:
            return "(" + " ".join(dump(x) for x in node) + ")"
        out = "(" + dump(node[0])
        for x in node[1:]:
            out += "\n" + pad + "\t" + dump(x, indent + 1) if isinstance(x, list) else " " + dump(x)
        return out + "\n" + pad + ")"
    if isinstance(node, Sym):
        return str(node)
    if isinstance(node, (int, float)):
        return fmt(node)
    return '"' + str(node).replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def fmt(v):
    s = ("%.4f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def find(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == key]


def first(node, key):
    r = find(node, key)
    return r[0] if r else None


_libs = {}


def lib(name):
    if name not in _libs:
        tree = parse(open(os.path.join(KICAD, "symbols", name + ".kicad_sym")).read())[0]
        _libs[name] = {x[1]: x for x in find(tree, "symbol")}
    return _libs[name]


def symbol(lib_id):
    """Library symbol with 'extends' resolved, renamed to 'Lib:Name' for lib_symbols."""
    import copy
    lname, sname = lib_id.split(":")
    s = copy.deepcopy(lib(lname)[sname])
    ext = first(s, "extends")
    if ext:
        parent = symbol(lname + ":" + ext[1])
        pname = ext[1]
        props = {p[1]: p for p in find(s, "property")}
        merged = [Sym("symbol"), sname]
        for x in parent[2:]:
            if isinstance(x, list) and x[0] == "property" and x[1] in props:
                merged.append(props.pop(x[1]))
            elif isinstance(x, list) and x[0] == "symbol":
                y = copy.deepcopy(x)
                y[1] = y[1].replace(pname.split(":")[-1], sname, 1)
                merged.append(y)
            else:
                merged.append(x)
        for p in props.values():
            merged.append(p)
        s = merged
    s[1] = lname + ":" + sname
    for sub in find(s, "symbol"):
        sub[1] = re.sub(r"^.*?(_\d+_\d+)$", sname + r"\1", sub[1])
    return s


def pins(lib_id):
    """[(number, name, x, y, angle, type)] of unit 1 / common, body style 1."""
    s = symbol(lib_id)
    out = []
    for sub in find(s, "symbol"):
        m = re.search(r"_(\d+)_(\d+)$", sub[1])
        unit, style = int(m.group(1)), int(m.group(2))
        if style not in (0, 1):
            continue
        for p in find(sub, "pin"):
            at = first(p, "at")
            out.append((first(p, "number")[1], first(p, "name")[1], float(at[1]), float(at[2]),
                        float(at[3]) if len(at) > 3 else 0.0, str(p[1]), unit))
    return out


def prop(lib_id, name):
    for p in find(symbol(lib_id), "property"):
        if p[1] == name:
            return p[2]
    return ""

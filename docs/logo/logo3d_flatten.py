"""Flatten a LaneTag logo SVG into non-overlapping, per-colour prism meshes.

Usage: python logo3d_flatten.py INPUT.svg OUTPUT.json [--width-mm 100]
Needs shapely >= 2.1 (pip install shapely).

The SVG is painted in document order (fills, strokes, clip paths, transforms),
each new shape cutting away what lies beneath it, so every colour ends up as
its own region that tiles the artwork with no overlaps. A backing plate in the
outline colour joins the separate pieces (spool, hardware, wordmark) and gives
the print a border. The JSON holds one triangulated, watertight mesh per part,
ready for logo3d_blender.py to turn into a coloured .blend / .obj.
"""
import argparse, json, math, re
import xml.etree.ElementTree as ET
import shapely
from shapely import affinity
from shapely.geometry import LineString, LinearRing, MultiPolygon, Point, Polygon, GeometryCollection
from shapely.ops import unary_union

NS = "{http://www.w3.org/2000/svg}"
ARC_STEP = math.radians(4)       # arc flattening resolution
CURVE_SEGS = 12                  # segments per bezier

# --- print layout (mm) -------------------------------------------------------
BASE_MM = 1.6          # backing plate thickness
TOP_MM = 1.0           # colour layer thickness, sits on the plate
BORDER_PX = 8           # backing border around the artwork, in SVG px
JOIN_PX = 12            # extra grow-then-shrink so the spool and wordmark share one plate
BACKING_COLOR = "#1b2632"
MIN_AREA_MM2 = 0.02    # drop slivers smaller than this
GRID_PX = 1e-3         # snap grid; keeps overlays between edge-sharing regions valid
GRID_MM = 1e-4


def snap(g, grid=GRID_PX):
    return shapely.set_precision(shapely.make_valid(g), grid)


# --- SVG path parsing --------------------------------------------------------
def tokens(d):
    for m in re.finditer(r"[MmLlHhVvCcSsQqTtAaZz]|[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?", d):
        yield m.group()


def arc_points(p0, rx, ry, phi_deg, large, sweep, p1):
    x1, y1 = p0
    x2, y2 = p1
    if rx == 0 or ry == 0:
        return [p1]
    phi = math.radians(phi_deg)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    lam = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx ** 2 * ry ** 2 - rx ** 2 * y1p ** 2 - ry ** 2 * x1p ** 2
    den = rx ** 2 * y1p ** 2 + ry ** 2 * x1p ** 2
    co = math.sqrt(max(0, num / den)) if den else 0
    if large == sweep:
        co = -co
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2

    def ang(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(2, int(abs(dt) / ARC_STEP))
    pts = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        ex, ey = rx * math.cos(t), ry * math.sin(t)
        pts.append((cp * ex - sp * ey + cx, sp * ex + cp * ey + cy))
    pts[-1] = p1
    return pts


def parse_path(d):
    """Return list of (points, closed) subpaths."""
    toks = list(tokens(d))
    i, cmd = 0, None
    subs, cur, closed = [], [], False
    pos = start = (0.0, 0.0)
    last_ctrl = None

    def num():
        nonlocal i
        v = float(toks[i]); i += 1
        return v

    def flush():
        nonlocal cur, closed
        if len(cur) > 1:
            subs.append((cur, closed))
        cur, closed = [], False

    while i < len(toks):
        if re.match(r"[A-Za-z]", toks[i]):
            cmd = toks[i]; i += 1
        elif cmd in ("M", "m"):
            cmd = "L" if cmd == "M" else "l"
        rel = cmd.islower()
        C = cmd.upper()
        ox, oy = pos if rel else (0.0, 0.0)
        if C == "Z":
            closed = True
            pos = start
            flush()
            last_ctrl = None
            continue
        if C == "M":
            flush()
            pos = (ox + num(), oy + num()); start = pos; cur = [pos]
        elif C == "L":
            pos = (ox + num(), oy + num()); cur.append(pos)
        elif C == "H":
            pos = ((ox if rel else 0) + num(), pos[1]); cur.append(pos)
        elif C == "V":
            pos = (pos[0], (oy if rel else 0) + num()); cur.append(pos)
        elif C in ("C", "S", "Q", "T"):
            if C == "C":
                c1 = (ox + num(), oy + num()); c2 = (ox + num(), oy + num())
            elif C == "S":
                c1 = (2 * pos[0] - last_ctrl[0], 2 * pos[1] - last_ctrl[1]) if last_ctrl else pos
                c2 = (ox + num(), oy + num())
            elif C == "Q":
                q = (ox + num(), oy + num())
            else:
                q = (2 * pos[0] - last_ctrl[0], 2 * pos[1] - last_ctrl[1]) if last_ctrl else pos
            end = (ox + num(), oy + num())
            p0 = pos
            for k in range(1, CURVE_SEGS + 1):
                t = k / CURVE_SEGS; u = 1 - t
                if C in ("C", "S"):
                    x = u**3*p0[0] + 3*u*u*t*c1[0] + 3*u*t*t*c2[0] + t**3*end[0]
                    y = u**3*p0[1] + 3*u*u*t*c1[1] + 3*u*t*t*c2[1] + t**3*end[1]
                else:
                    x = u*u*p0[0] + 2*u*t*q[0] + t*t*end[0]
                    y = u*u*p0[1] + 2*u*t*q[1] + t*t*end[1]
                cur.append((x, y))
            last_ctrl = c2 if C in ("C", "S") else q
            pos = end
            continue
        elif C == "A":
            rx, ry, phi = num(), num(), num()
            large, sweep = int(num()), int(num())
            end = (ox + num(), oy + num())
            cur.extend(arc_points(pos, rx, ry, phi, large, sweep, end))
            pos = end
        last_ctrl = None
    flush()
    return subs


# --- transforms --------------------------------------------------------------
def mul(a, b):  # affine matrices as (a,b,c,d,e,f) like SVG matrix()
    return (a[0]*b[0] + a[2]*b[1], a[1]*b[0] + a[3]*b[1],
            a[0]*b[2] + a[2]*b[3], a[1]*b[2] + a[3]*b[3],
            a[0]*b[4] + a[2]*b[5] + a[4], a[1]*b[4] + a[3]*b[5] + a[5])


IDENT = (1, 0, 0, 1, 0, 0)


def parse_transform(s):
    m = IDENT
    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", s or ""):
        v = [float(x) for x in re.split(r"[\s,]+", args.strip()) if x]
        if name == "translate":
            t = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0)
        elif name == "scale":
            t = (v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0)
        elif name == "rotate":
            a = math.radians(v[0]); c, s_ = math.cos(a), math.sin(a)
            t = (c, s_, -s_, c, 0, 0)
            if len(v) == 3:
                t = mul(mul((1, 0, 0, 1, v[1], v[2]), t), (1, 0, 0, 1, -v[1], -v[2]))
        elif name == "matrix":
            t = tuple(v)
        else:
            raise ValueError(name)
        m = mul(m, t)
    return m


def apply(g, m):
    return affinity.affine_transform(g, [m[0], m[2], m[1], m[3], m[4], m[5]])


# --- element geometry --------------------------------------------------------
def ellipse_ring(cx, cy, rx, ry, n=144):
    return [(cx + rx * math.cos(2*math.pi*k/n), cy + ry * math.sin(2*math.pi*k/n)) for k in range(n)]


def rect_ring(x, y, w, h, rx):
    if rx <= 0:
        return [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
    pts = []
    for cx, cy, a0 in ((x+w-rx, y+rx, -90), (x+w-rx, y+h-rx, 0), (x+rx, y+h-rx, 90), (x+rx, y+rx, 180)):
        for k in range(13):
            a = math.radians(a0 + 90 * k / 12)
            pts.append((cx + rx * math.cos(a), cy + rx * math.sin(a)))
    return pts


def subpaths(el):
    tag = el.tag.replace(NS, "")
    g = lambda k, d=0: float(el.get(k, d))
    if tag == "path":
        return parse_path(el.get("d"))
    if tag == "ellipse":
        return [(ellipse_ring(g("cx"), g("cy"), g("rx"), g("ry")), True)]
    if tag == "circle":
        return [(ellipse_ring(g("cx"), g("cy"), g("r"), g("r")), True)]
    if tag == "rect":
        return [(rect_ring(g("x"), g("y"), g("width"), g("height"), g("rx")), True)]
    return []


def fill_geom(subs):
    """Even-odd fill of all subpaths (font outlines and these shapes don't self-overlap)."""
    g = Polygon()
    for pts, _ in subs:
        if len(pts) >= 3:
            p = shapely.make_valid(Polygon(pts))
            g = g.symmetric_difference(p)
    return g


CAP = {"butt": "flat", "round": "round", "square": "square"}
JOIN = {"miter": "mitre", "round": "round", "bevel": "bevel"}


def stroke_geom(subs, width, cap, join):
    parts = []
    for pts, closed in subs:
        line = LinearRing(pts) if closed and len(pts) >= 3 else LineString(pts)
        parts.append(line.buffer(width / 2, quad_segs=8, cap_style=CAP[cap],
                                 join_style=JOIN[join], mitre_limit=4))
    return unary_union(parts)


# --- painter -----------------------------------------------------------------
INHERIT = ("fill", "stroke", "stroke-width", "stroke-linecap", "stroke-linejoin")


def flatten(svg_path):
    root = ET.parse(svg_path).getroot()
    clips = {}
    layers = []  # list of [color, geometry]

    def paint(color, geom):
        geom = snap(geom)
        if geom.is_empty:
            return
        for L in layers:
            L[1] = shapely.difference(L[1], geom, grid_size=GRID_PX)
        layers.append([color, geom])

    def walk(el, ctm, style, clip):
        tag = el.tag.replace(NS, "")
        if tag in ("title", "desc", "defs"):
            return
        if tag == "clipPath":
            geo = unary_union([fill_geom(subpaths(c)) for c in el])
            clips[el.get("id")] = apply(geo, ctm)
            return
        ctm = mul(ctm, parse_transform(el.get("transform")))
        style = dict(style)
        for k in INHERIT:
            if el.get(k) is not None:
                style[k] = el.get(k)
        cp = el.get("clip-path")
        if cp:
            c = clips[re.search(r"#([^)]+)", cp).group(1)]
            clip = c if clip is None else clip.intersection(c)
        if tag in ("svg", "g"):
            for c in el:
                walk(c, ctm, style, clip)
            return
        subs = subpaths(el)
        if not subs:
            return
        for kind in ("fill", "stroke"):
            color = style.get(kind, "#000000" if kind == "fill" else "none")
            if color == "none":
                continue
            if kind == "fill":
                geo = fill_geom(subs)
            else:
                geo = stroke_geom(subs, float(style.get("stroke-width", 1)),
                                  style.get("stroke-linecap", "butt"),
                                  style.get("stroke-linejoin", "miter"))
            geo = apply(geo, ctm)
            if clip is not None:
                geo = geo.intersection(clip)
            paint(color.lower(), shapely.make_valid(geo))

    walk(root, IDENT, {}, None)
    vb = [float(v) for v in root.get("viewBox").split()]
    return layers, vb


def polys(g):
    if isinstance(g, Polygon):
        return [g] if not g.is_empty else []
    if hasattr(g, "geoms"):
        return [p for sub in g.geoms for p in polys(sub)]
    return []


# --- meshing -----------------------------------------------------------------
def prism(poly, z0, z1, verts, faces, vidx):
    poly = shapely.geometry.polygon.orient(poly, 1.0)

    def vid(x, y, z):
        k = (round(x, 5), round(y, 5), round(z, 5))
        if k not in vidx:
            vidx[k] = len(verts); verts.append(k)
        return vidx[k]

    tris = shapely.constrained_delaunay_triangles(poly)
    for t in tris.geoms:
        (ax, ay), (bx, by), (cx, cy) = list(t.exterior.coords)[:3]
        if (bx - ax) * (cy - ay) - (by - ay) * (cx - ax) < 0:
            bx, by, cx, cy = cx, cy, bx, by
        faces.append((vid(ax, ay, z1), vid(bx, by, z1), vid(cx, cy, z1)))
        faces.append((vid(ax, ay, z0), vid(cx, cy, z0), vid(bx, by, z0)))
    for ring in [poly.exterior, *poly.interiors]:
        c = list(ring.coords)
        for (ax, ay), (bx, by) in zip(c, c[1:]):
            faces.append((vid(ax, ay, z0), vid(bx, by, z0), vid(bx, by, z1), vid(ax, ay, z1)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("svg"); ap.add_argument("out")
    ap.add_argument("--width-mm", type=float, default=100.0)
    a = ap.parse_args()

    layers, vb = flatten(a.svg)
    art = shapely.union_all([g for _, g in layers], grid_size=GRID_PX)
    # backing: artwork silhouette grown by the border, rounded, with holes filled
    grown = art.buffer(BORDER_PX + JOIN_PX, quad_segs=16).buffer(-JOIN_PX, quad_segs=16)
    backing = unary_union([Polygon(p.exterior) for p in polys(grown)])
    backing = snap(backing)
    ring = shapely.difference(backing, art, grid_size=GRID_PX)

    regions = {}
    for color, g in layers:
        regions[color] = shapely.union_all([regions.get(color, Polygon()), g], grid_size=GRID_PX)
    regions[BACKING_COLOR] = shapely.union_all([regions.get(BACKING_COLOR, Polygon()), ring], grid_size=GRID_PX)

    # px -> mm, flip y so the logo reads correctly from above (+Z)
    minx, miny, maxx, maxy = backing.bounds
    s = a.width_mm / (maxx - minx)
    cx, cy = (minx + maxx) / 2, (miny + maxy) / 2
    to_mm = lambda g: affinity.affine_transform(g, [s, 0, 0, -s, -cx * s, cy * s])

    parts = []

    def add(name, color, geom, z0, z1):
        verts, faces, vidx = [], [], {}
        ps = [p for p in polys(snap(to_mm(geom), GRID_MM)) if p.area >= MIN_AREA_MM2]
        for p in ps:
            prism(p, z0, z1, verts, faces, vidx)
        if faces:
            parts.append(dict(name=name, color=color, verts=verts, faces=faces))
            print(f"{name:22s} {color}  {len(ps):4d} islands  {sum(p.area for p in ps):9.1f} mm2")

    add("base_plate", BACKING_COLOR, backing, 0.0, BASE_MM)
    for color, g in regions.items():
        add("color_" + color.lstrip("#"), color, g, BASE_MM, BASE_MM + TOP_MM)

    w = (maxx - minx) * s; h = (maxy - miny) * s
    print(f"size: {w:.1f} x {h:.1f} x {BASE_MM + TOP_MM:.1f} mm, {len(parts)} parts")
    with open(a.out, "w") as f:
        json.dump(dict(size_mm=[w, h, BASE_MM + TOP_MM], parts=parts), f)


if __name__ == "__main__":
    main()

"""Generate the LaneTag logo SVGs (full lockup, dark variant, spool mark).

Usage: python make_logo.py Poppins-Bold.ttf OUTPUT_DIR
The wordmark is Poppins Bold (SIL OFL), converted to outlines.
"""
import math, sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

FONT = sys.argv[1]
OUT = sys.argv[2]

# palette
SLATE_D = "#243241"   # back flange, outlines
SLATE = "#3a4e63"     # front face
SLATE_L = "#53697f"   # flange rim / bore wall
HOLE = "#141c25"
ORANGE = "#f28a1e"
ORANGE_D = "#c9650f"
RED = "#d93a2f"
BLUE = "#1e88e5"
INK = "#1b2632"
WHITE = "#ffffff"
GREY = "#c8d0d8"


STICKER_ANGLE = 135
CARD_C, CARD_DEG = (74, 96), -8
ESP_C, ESP_DEG = (108, 296), 5


def f(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s == "-0" else s


class Spool:
    def __init__(self, cx, cy, rx, ry, depth, rim):
        self.F = (cx, cy)
        self.rx, self.ry = rx, ry
        self.D = depth
        self.t = rim

    def at(self, k):
        """centre of the slice at fraction k along the axis (0 = front face)"""
        return (self.F[0] + self.D[0] * k, self.F[1] + self.D[1] * k)

    def pt(self, c, r, a):
        return (c[0] + r * self.rx * math.cos(a), c[1] - r * self.ry * math.sin(a))

    def ell(self, c, r, **attrs):
        a = " ".join(f'{k.replace("_", "-")}="{v}"' for k, v in attrs.items())
        return f'<ellipse cx="{f(c[0])}" cy="{f(c[1])}" rx="{f(r*self.rx)}" ry="{f(r*self.ry)}" {a}/>'

    def tangent_angles(self, r):
        # silhouette points where the ellipse tangent is parallel to the depth axis
        # p'(a) = (-rx sin a, -ry cos a) parallel to D  =>  tan a = ry*dx / (rx*dy)
        dx, dy = self.D
        a = math.atan2(self.ry * dx, self.rx * dy)
        b = a + math.pi
        return (a, b) if math.sin(a) > 0 else (b, a)


def arc_path(sp, c, r, a0, a1, sweep):
    p0 = sp.pt(c, r, a0)
    p1 = sp.pt(c, r, a1)
    large = 1 if abs(a1 - a0) % (2 * math.pi) > math.pi else 0
    return f"M{f(p0[0])} {f(p0[1])}A{f(r*sp.rx)} {f(r*sp.ry)} 0 {large} {sweep} {f(p1[0])} {f(p1[1])}"


def sector(sp, c, r1, r2, mid, half):
    a0, a1 = mid - half, mid + half
    o0, o1 = sp.pt(c, r2, a0), sp.pt(c, r2, a1)
    i1, i0 = sp.pt(c, r1, a1), sp.pt(c, r1, a0)
    return (f"M{f(o0[0])} {f(o0[1])}A{f(r2*sp.rx)} {f(r2*sp.ry)} 0 0 0 {f(o1[0])} {f(o1[1])}"
            f"L{f(i1[0])} {f(i1[1])}A{f(r1*sp.rx)} {f(r1*sp.ry)} 0 0 1 {f(i0[0])} {f(i0[1])}Z")


def spool_svg(sp, uid):
    out = []
    back = sp.at(1.0)
    fil_front, fil_back = sp.at(0.08), sp.at(0.92)
    RF = 0.86  # filament outer radius relative to flange

    out.append('<g id="spool">')
    # back flange
    out.append(sp.ell(back, 1.0, id="back-flange", fill=SLATE_D, stroke=INK, stroke_width=3))

    # filament body = hull of front and back filament ellipses
    top, bot = sp.tangent_angles(RF)
    ft, bt = sp.pt(fil_front, RF, top), sp.pt(fil_back, RF, top)
    bb, fb = sp.pt(fil_back, RF, bot), sp.pt(fil_front, RF, bot)
    rx, ry = RF * sp.rx, RF * sp.ry
    body = (f"M{f(ft[0])} {f(ft[1])}L{f(bt[0])} {f(bt[1])}A{f(rx)} {f(ry)} 0 0 1 {f(bb[0])} {f(bb[1])}"
            f"L{f(fb[0])} {f(fb[1])}A{f(rx)} {f(ry)} 0 0 1 {f(ft[0])} {f(ft[1])}Z")
    out.append(f'<path id="filament" d="{body}" fill="{ORANGE}" stroke="{INK}" stroke-width="3" stroke-linejoin="round"/>')
    # coil windings: far-side half ellipses at evenly spaced depths
    out.append(f'<g id="filament-coils" fill="none" stroke="{ORANGE_D}" stroke-width="2.5" stroke-linecap="round">')
    for k in (0.22, 0.36, 0.5, 0.64, 0.78):
        c = (fil_front[0] + (fil_back[0] - fil_front[0]) * k / 0.92 * 1.0,
             fil_front[1] + (fil_back[1] - fil_front[1]) * k / 0.92 * 1.0)
        out.append(f'<path d="{arc_path(sp, c, RF, top, bot, 1)}"/>')
    out.append("</g>")

    # front flange rim (thickness) then face
    out.append(sp.ell(sp.at(0.0), 1.0, id="front-rim", fill=SLATE_L, stroke=INK, stroke_width=3,
                      transform=f"translate({f(sp.t[0])} {f(sp.t[1])})"))
    out.append(sp.ell(sp.F, 1.0, id="front-face", fill=SLATE, stroke=INK, stroke_width=3))

    # four recessed windows showing the coil behind
    out.append('<g id="windows">')
    shift = (7, -0.5)
    for i, mid in enumerate((90, 0, 270, 180)):
        d = sector(sp, sp.F, 0.4, 0.75, math.radians(mid), math.radians(21))
        cid = f"{uid}-win{i}"
        out.append(f'<clipPath id="{cid}"><path d="{d}"/></clipPath>')
        out.append(f'<path d="{d}" fill="{SLATE_D}" stroke="{SLATE_D}" stroke-width="6" stroke-linejoin="round"/>')
        out.append(f'<g clip-path="url(#{cid})">')
        out.append(f'<path d="{d}" fill="{ORANGE}" transform="translate({shift[0]} {shift[1]})"/>')
        for r in (0.5, 0.62):
            out.append(f'<path d="{arc_path(sp, (sp.F[0]+shift[0], sp.F[1]+shift[1]), r, math.radians(mid+30), math.radians(mid-30), 1)}" '
                       f'fill="none" stroke="{ORANGE_D}" stroke-width="2.5"/>')
        out.append("</g>")
        out.append(f'<path d="{d}" fill="none" stroke="{INK}" stroke-width="2.5" stroke-linejoin="round"/>')
    out.append("</g>")

    # central bore: wall crescent + dark far opening
    cid = f"{uid}-bore"
    out.append(f'<clipPath id="{cid}">{sp.ell(sp.F, 0.2)}</clipPath>')
    out.append('<g id="bore">')
    out.append(sp.ell(sp.F, 0.2, fill=SLATE_L))
    far = (sp.F[0] + 12, sp.F[1] - 1)
    out.append(f'<g clip-path="url(#{cid})">{sp.ell(far, 0.2, fill=HOLE)}</g>')
    out.append(sp.ell(sp.F, 0.2, fill="none", stroke=INK, stroke_width=3))
    out.append("</g>")

    # NFC sticker between the top and left windows, facing the reader; the
    # radio-wave icon is scaled with the face so it sits flat in perspective
    sc = sp.pt(sp.F, 0.6, math.radians(STICKER_ANGLE))
    srx, sry = 0.19 * sp.rx, 0.19 * sp.ry
    out.append('<g id="nfc-sticker">')
    out.append(f'<ellipse cx="{f(sc[0])}" cy="{f(sc[1])}" rx="{f(srx)}" ry="{f(sry)}" fill="{WHITE}" stroke="{INK}" stroke-width="2.5"/>')
    out.append(f'<g id="tag-waves" transform="translate({f(sc[0])} {f(sc[1])}) scale({f(srx*0.8)} {f(sry*0.8)})" '
               f'fill="none" stroke="{BLUE}" stroke-width="0.13" stroke-linecap="round">')
    out.append(f'<circle r="0.13" fill="{BLUE}" stroke="none"/>')
    for r in (0.38, 0.66, 0.94):
        x, y = r * math.cos(math.radians(48)), r * math.sin(math.radians(48))
        out.append(f'<path d="M{f(-x)} {f(-y)}A{r} {r} 0 0 0 {f(-x)} {f(y)}M{f(x)} {f(-y)}A{r} {r} 0 0 1 {f(x)} {f(y)}"/>')
    out.append("</g>")
    out.append("</g>")
    out.append("</g>")
    return "\n".join(out)


NFC_PATHS = [
    "M32 148L42 84L118 150L126 86",
    "M166 62A85 85 0 0 1 166 178",
    "M206 32A120 120 0 0 1 206 208",
]


def nfc_symbol(x, y, scale):
    s = [f'<g id="nfc-symbol" transform="translate({f(x)} {f(y)}) scale({f(scale)})" fill="none" stroke="{BLUE}" '
         f'stroke-width="24" stroke-linecap="round" stroke-linejoin="round">']
    s += [f'<path d="{d}"/>' for d in NFC_PATHS]
    s.append("</g>")
    return "\n".join(s)


def rot(px, py, cx, cy, deg):
    a = math.radians(deg)
    dx, dy = px - cx, py - cy
    return cx + dx * math.cos(a) - dy * math.sin(a), cy + dx * math.sin(a) + dy * math.cos(a)


def rfid_glyphs(x, baseline, size, color):
    """'RFID' set in the wordmark font, centred on x"""
    font = TTFont(FONT)
    gs, cmap = font.getGlyphSet(), font.getBestCmap()
    s = size / font["head"].unitsPerEm
    w = sum(gs[cmap[ord(c)]].width for c in "RFID") * s
    px, ds = x - w / 2, []
    for c in "RFID":
        pen = SVGPathPen(gs, ntos=lambda v: f(v))
        gs[cmap[ord(c)]].draw(TransformPen(pen, (s, 0, 0, -s, px, baseline)))
        ds.append(pen.getCommands())
        px += gs[cmap[ord(c)]].width * s
    return f'<path id="rfid-text" fill="{color}" d="{"".join(ds)}"/>'


def card(cx, cy, deg):
    """red reader card centred on (cx, cy), rotated deg; returns (svg, cable anchor)"""
    cw = 112
    x, y = cx - cw / 2, cy - cw / 2
    o = [f'<g id="nfc-card" transform="rotate({deg} {cx} {cy})">']
    o.append(f'<rect x="{f(x+cw/2-6)}" y="{f(y+cw-4)}" width="12" height="14" rx="2" fill="{INK}"/>')
    o.append(f'<rect x="{f(x)}" y="{f(y)}" width="{cw}" height="{cw}" rx="13" fill="{RED}" stroke="{INK}" stroke-width="3"/>')
    # NFC badge, top left
    bx, by = x + 32, y + 34
    o.append(f'<circle cx="{f(bx)}" cy="{f(by)}" r="22" fill="{WHITE}" stroke="{INK}" stroke-width="2.5"/>')
    sc = 0.15
    o.append(nfc_symbol(bx - 133 * sc, by - 120 * sc, sc))
    # RFID mark, top right: quarter arcs round a dot
    dx, dy = x + cw - 18, y + 58
    o.append(f'<g id="rfid-mark" fill="none" stroke="{WHITE}" stroke-width="5.5">')
    for r in (14, 25, 36):
        o.append(f'<path d="M{f(dx-r)} {f(dy)}A{r} {r} 0 0 1 {f(dx)} {f(dy-r)}"/>')
    o.append("</g>")
    o.append(f'<circle cx="{f(dx)}" cy="{f(dy)}" r="5.5" fill="{WHITE}"/>')
    o.append(rfid_glyphs(x + cw / 2, y + cw - 14, 27, WHITE))
    o.append("</g>")
    return "\n".join(o), rot(cx, y + cw + 10, cx, cy, deg)


def esp32(cx, cy, deg):
    bw, bh = 76, 104
    bx, by = cx - bw / 2, cy - bh / 2
    o = [f'<g id="esp32" transform="rotate({deg} {cx} {cy})">']
    o.append(f'<rect x="{f(bx)}" y="{f(by)}" width="{bw}" height="{bh}" rx="8" fill="{SLATE_D}" stroke="{INK}" stroke-width="3"/>')
    mx, my = bx + 20, by + 12
    o.append(f'<path d="M{f(mx)} {f(my+12)}V{f(my)}H{f(mx+9)}V{f(my+10)}H{f(mx+18)}V{f(my)}H{f(mx+27)}V{f(my+10)}H{f(mx+36)}V{f(my)}" '
             f'fill="none" stroke="{GREY}" stroke-width="2.5" stroke-linejoin="round"/>')
    o.append(f'<rect x="{f(bx+16)}" y="{f(by+30)}" width="{bw-32}" height="40" rx="3" fill="{GREY}" stroke="{INK}" stroke-width="2"/>')
    for i in range(6):
        py = by + 14 + i * 14
        o.append(f'<circle cx="{f(bx+8)}" cy="{f(py)}" r="2.6" fill="{GREY}"/>')
        o.append(f'<circle cx="{f(bx+bw-8)}" cy="{f(py)}" r="2.6" fill="{GREY}"/>')
    o.append(f'<rect x="{f(bx+bw/2-11)}" y="{f(by+bh-12)}" width="22" height="9" rx="2" fill="{GREY}" stroke="{INK}" stroke-width="2"/>')
    o.append("</g>")
    return "\n".join(o), rot(bx + 8, by + 14, cx, cy, deg)


def hardware(card_c, card_deg, esp_c, esp_deg):
    c_svg, a0 = card(*card_c, card_deg)
    e_svg, a1 = esp32(*esp_c, esp_deg)
    # light cable with a dark casing so it reads over the slate flange and on any background
    d = f"M{f(a0[0])} {f(a0[1])}C{f(a0[0]-4)} {f(a0[1]+40)} {f(a1[0]-34)} {f(a1[1]-40)} {f(a1[0])} {f(a1[1])}"
    cable = (f'<g id="cable" fill="none" stroke-linecap="round">'
             f'<path d="{d}" stroke="{INK}" stroke-width="10"/><path d="{d}" stroke="{GREY}" stroke-width="5"/></g>')
    return '<g id="hardware">\n' + "\n".join([cable, e_svg, c_svg]) + "\n</g>"


def wordmark(text_parts, x, baseline, size, tracking=-0.01):
    font = TTFont(FONT)
    gs = font.getGlyphSet()
    cmap = font.getBestCmap()
    upm = font["head"].unitsPerEm
    s = size / upm
    o = [f'<g id="wordmark" aria-label="{"".join(t for t, _ in text_parts)}">']
    pen_x = x
    for text, color in text_parts:
        ds = []
        for ch in text:
            g = cmap[ord(ch)]
            sp = SVGPathPen(gs, ntos=lambda v: f(v))
            gs[g].draw(TransformPen(sp, (s, 0, 0, -s, pen_x, baseline)))
            ds.append(sp.getCommands())
            pen_x += gs[g].width * s + tracking * size
        o.append(f'<path id="word-{text.lower()}" fill="{color}" d="{"".join(ds)}"/>')
    o.append("</g>")
    return "\n".join(o), pen_x


def doc(w, h, body, title):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}">\n'
            f"<title>{title}</title>\n{body}\n</svg>\n")


# full lockup: spool shifted right so the reader + ESP32 overlap its left side
sp = Spool(212, 200, 112, 150, (80, -6), (11, -1))
spool = spool_svg(sp, "lt")
hw = hardware(CARD_C, CARD_DEG, ESP_C, ESP_DEG)
wm, end_x = wordmark([("Lane", SLATE_D), ("Tag", ORANGE)], 444, 244, 128)
W = math.ceil(end_x + 20)
open(OUT + "/lanetag-logo.svg", "w", encoding="utf-8").write(doc(W, 400, "\n".join([spool, hw, wm]), "LaneTag"))
# dark-background variant: only "Lane" changes colour
wm_dark, _ = wordmark([("Lane", "#eef2f6"), ("Tag", ORANGE)], 444, 244, 128)
open(OUT + "/lanetag-logo-dark.svg", "w", encoding="utf-8").write(doc(W, 400, "\n".join([spool, hw, wm_dark]), "LaneTag"))

# icon mark: spool only, square
sp2 = Spool(150, 200, 112, 150, (80, -6), (11, -1))
open(OUT + "/lanetag-mark.svg", "w", encoding="utf-8").write(
    doc(400, 400, f'<g transform="translate(36 0)">{spool_svg(sp2, "mk")}</g>', "LaneTag"))
print("width", W)

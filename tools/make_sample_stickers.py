#!/usr/bin/env python3
"""Generates original sample .tgs stickers (gzip Lottie, 512x512, 60 fps, <= 3 s)
into examples/dll_dx11/assets. Pure stdlib. Usage: python make_sample_stickers.py [out_dir]"""
import gzip, json, math, os, sys

W = H = 512
FPS = 60
EASE = (0.42, 0.0, 0.58, 1.0)


def st(v):  # static property
    return {"a": 0, "k": v}


def kf(pairs, ease=EASE):
    """pairs: [(frame, value), ...] -> animated property with bezier easing"""
    out = []
    for i, (t, v) in enumerate(pairs):
        v = v if isinstance(v, list) else [v]
        k = {"t": t, "s": v}
        if i < len(pairs) - 1:
            k["o"] = {"x": [ease[0]], "y": [ease[1]]}
            k["i"] = {"x": [ease[2]], "y": [ease[3]]}
        out.append(k)
    return {"a": 1, "k": out}


def tr(p=(0, 0), a=(0, 0), s=(100, 100), r=0, o=100):
    def wrap(v):
        return v if isinstance(v, dict) else st(list(v) if isinstance(v, tuple) else v)
    return {"ty": "tr", "p": wrap(p), "a": wrap(a), "s": wrap(s), "r": wrap(r), "o": wrap(o),
            "sk": st(0), "sa": st(0), "nm": "Transform"}


def layer_ks(p=(256, 256), a=(0, 0), s=(100, 100), r=0, o=100):
    def wrap(v, dims):
        if isinstance(v, dict):
            return v
        if isinstance(v, tuple):
            return st(list(v) + [0] * (dims - len(v)))
        return st(v)
    return {"o": wrap(o, 1), "r": wrap(r, 1), "p": wrap(p, 3), "a": wrap(a, 3), "s": wrap(s, 3)}


def group(items, transform=None, nm="Group"):
    return {"ty": "gr", "nm": nm, "it": items + [transform or tr()]}


def ellipse(size, pos=(0, 0)):
    return {"ty": "el", "d": 1, "p": st(list(pos)), "s": size if isinstance(size, dict) else st(list(size))}


def rect(size, pos=(0, 0), r=0):
    return {"ty": "rc", "d": 1, "p": st(list(pos)), "s": st(list(size)), "r": st(r)}


def star(points, outer, inner, oround=0, iround=0, rot=0):
    return {"ty": "sr", "sy": 1, "d": 1, "pt": st(points), "p": st([0, 0]), "r": st(rot),
            "or": st(outer), "ir": st(inner), "os": st(oround), "is": st(iround)}


def path(verts, ins, outs, closed=True):
    return {"ty": "sh", "ks": st({"v": verts, "i": ins, "o": outs, "c": closed})}


def fill(rgb, o=100):
    c = rgb if isinstance(rgb, dict) else st(list(rgb) + [1])
    return {"ty": "fl", "c": c, "o": o if isinstance(o, dict) else st(o), "r": 1}


def stroke(rgb, w, o=100):
    return {"ty": "st", "c": st(list(rgb) + [1]), "o": o if isinstance(o, dict) else st(o),
            "w": w if isinstance(w, dict) else st(w), "lc": 2, "lj": 2}


def radial(stops, start, end):
    flat = []
    for p, (r, g, b) in stops:
        flat += [p, r, g, b]
    return {"ty": "gf", "o": st(100), "r": 1, "t": 2, "s": st(list(start)), "e": st(list(end)),
            "h": st(0), "a": st(0), "g": {"p": len(stops), "k": st(flat)}}


def shape_layer(ind, nm, shapes, ks, op):
    return {"ddd": 0, "ind": ind, "ty": 4, "nm": nm, "sr": 1, "ks": ks, "ao": 0,
            "shapes": shapes, "ip": 0, "op": op, "st": 0, "bm": 0}


def comp(nm, op, layers):
    # tgs requires: "tgs": 1, 512x512, fps 30/60, duration <= 3 s
    return {"tgs": 1, "v": "5.5.2", "fr": FPS, "ip": 0, "op": op, "w": W, "h": H, "nm": nm,
            "ddd": 0, "assets": [], "layers": layers}


def hex_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))


# --------------------------------------------------------------------------- stickers
def star_spin():
    op = 120
    gold, orange, cream = hex_rgb("ffc93c"), hex_rgb("ff8a00"), hex_rgb("fff3c4")
    glow = shape_layer(3, "glow", [group([ellipse((400, 400)), fill(cream)])],
                       layer_ks(o=kf([(0, 25), (60, 55), (120, 25)]),
                                s=kf([(0, [90, 90, 100]), (60, [105, 105, 100]), (120, [90, 90, 100])])), op)
    body = shape_layer(2, "star", [group([star(5, 92, 42), fill(hex_rgb("fff08a"))], nm="inner"),
                                   group([star(5, 175, 80, 0, 0), stroke(orange, 14), fill(gold)], nm="outer")],
                       layer_ks(r=kf([(0, 0), (120, 72)]),
                                s=kf([(0, [100, 100, 100]), (30, [114, 114, 100]), (60, [100, 100, 100]),
                                      (90, [110, 110, 100]), (120, [100, 100, 100])])), op)
    sparkles = []
    for i, (x, y, d) in enumerate([(90, 100, 0), (420, 120, 20), (430, 410, 40), (100, 400, 60)]):
        s = kf([(d, [0, 0, 100]), (d + 20, [100, 100, 100]), (d + 40, [0, 0, 100]), (op, [0, 0, 100])])
        sparkles.append(shape_layer(10 + i, f"sparkle{i}", [group([star(4, 42, 11), fill(hex_rgb("ffa94d"))])],
                                    layer_ks(p=(x, y), s=s, r=kf([(0, 0), (op, 90)])), op))
    return comp("star_spin", op, sparkles + [body, glow])


def ball_bounce():
    op = 120
    red, dark = hex_rgb("ff4d5a"), hex_rgb("c81e3a")
    ground = 430
    half = op // 2
    y = []
    s = []
    for b in range(2):
        t0 = b * half
        y += [(t0, [256, 170, 0]), (t0 + 26, [256, ground, 0]), (t0 + 34, [256, ground, 0])]
        s += [(t0, [100, 100, 100]), (t0 + 24, [92, 110, 100]), (t0 + 28, [128, 74, 100]),
              (t0 + 34, [96, 106, 100]), (t0 + 44, [100, 100, 100])]
    y.append((op, [256, 170, 0]))
    s.append((op, [100, 100, 100]))
    ball = shape_layer(1, "ball", [
        group([ellipse((52, 34), (-34, -118)), fill((1, 1, 1), 55)], nm="highlight"),
        group([ellipse((150, 150), (0, -75)), stroke(dark, 8), fill(red)], nm="body"),
    ], layer_ks(p=kf(y), s=kf(s)), op)
    shadow_s, shadow_o = [], []
    for b in range(2):
        t0 = b * half
        shadow_s += [(t0, [55, 55, 100]), (t0 + 26, [115, 115, 100]), (t0 + 34, [115, 115, 100])]
        shadow_o += [(t0, 12), (t0 + 26, 35), (t0 + 34, 35)]
    shadow_s.append((op, [55, 55, 100]))
    shadow_o.append((op, 12))
    shadow = shape_layer(2, "shadow", [group([ellipse((170, 34)), fill((0, 0, 0))])],
                         layer_ks(p=(256, ground + 8), s=kf(shadow_s), o=kf(shadow_o)), op)
    return comp("ball_bounce", op, [ball, shadow])


def heart_beat():
    op = 120
    red = hex_rgb("ff3b6b")
    # heart: 6 vertices, clockwise from the top dip; tangents are relative to the vertex
    v = [[0, -75], [65, -128], [128, -50], [0, 125], [-128, -50], [-65, -128]]
    i_ = [[-15, -35], [-35, 0], [0, -45], [70, -60], [0, 50], [-35, 0]]
    o_ = [[15, -35], [35, 0], [0, 50], [-70, -60], [0, -45], [35, 0]]
    heart = shape_layer(1, "heart", [group([path(v, i_, o_), stroke(hex_rgb("b3123f"), 10), fill(red)]),
                                     group([ellipse((46, 30), (-62, -82)), fill((1, 1, 1), 45)])],
                        layer_ks(p=(256, 266), s=kf([(0, [100, 100, 100]), (10, [118, 118, 100]),
                                                     (22, [100, 100, 100]), (32, [112, 112, 100]),
                                                     (50, [100, 100, 100]), (60, [100, 100, 100]),
                                                     (70, [118, 118, 100]), (82, [100, 100, 100]),
                                                     (92, [112, 112, 100]), (110, [100, 100, 100]),
                                                     (120, [100, 100, 100])])), op)
    rings = []
    for n, t0 in enumerate((0, 60)):
        rings.append(shape_layer(5 + n, f"ring{n}", [group([ellipse((300, 300)), stroke(red, 12)])],
                                 layer_ks(s=kf([(t0, [60, 60, 100]), (t0 + 55, [165, 165, 100]), (op, [165, 165, 100])]),
                                          o=kf([(t0, 90), (t0 + 55, 0), (op, 0)])), op))
    return comp("heart_beat", op, [heart] + rings)


def loader_dots():
    op = 90
    cols = [hex_rgb("4dabf7"), hex_rgb("845ef7"), hex_rgb("f06595")]
    layers = []
    for i, c in enumerate(cols):
        d = i * 12
        y = kf([(0, [136 + i * 120, 290, 0]), (d, [136 + i * 120, 290, 0]), (d + 18, [136 + i * 120, 200, 0]),
                (d + 36, [136 + i * 120, 290, 0]), (op, [136 + i * 120, 290, 0])])
        sc = kf([(0, [100, 100, 100]), (d + 36, [100, 100, 100]), (d + 40, [118, 84, 100]),
                 (d + 48, [100, 100, 100]), (op, [100, 100, 100])])
        layers.append(shape_layer(i + 1, f"dot{i}", [group([ellipse((86, 86)), fill(c)])],
                                  layer_ks(p=y, s=sc), op))
    return comp("loader_dots", op, layers)


def orb_ring():
    op = 180
    orb = shape_layer(2, "orb", [group([ellipse((250, 250)),
                                        radial([(0, hex_rgb("e0fbff")), (0.45, hex_rgb("38d9a9")), (1, hex_rgb("0b7285"))],
                                               (-40, -50), (125, 0))])],
                      layer_ks(s=kf([(0, [100, 100, 100]), (90, [108, 108, 100]), (180, [100, 100, 100])])), op)
    repeater = {"ty": "rp", "nm": "Repeater", "c": st(10), "o": st(0), "m": 1,
                "tr": {"ty": "tr", "p": st([0, 0]), "a": st([0, 0]), "s": st([100, 100]), "r": st(36),
                       "so": st(100), "eo": st(25)}}
    ring = shape_layer(1, "ring", [group([ellipse((34, 34), (0, -200)), fill(hex_rgb("63e6be")), repeater])],
                       layer_ks(r=kf([(0, 0), (op, 360)], ease=(0, 0, 1, 1))), op)
    return comp("orb_ring", op, [ring, orb])


STICKERS = {
    "star_spin": star_spin,
    "ball_bounce": ball_bounce,
    "heart_beat": heart_beat,
    "loader_dots": loader_dots,
    "orb_ring": orb_ring,
}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "examples", "dll_dx11", "assets")
    os.makedirs(out_dir, exist_ok=True)
    for name, fn in STICKERS.items():
        data = json.dumps(fn(), separators=(",", ":")).encode()
        path = os.path.join(out_dir, name + ".tgs")
        with open(path, "wb") as f:
            # mtime=0 -> reproducible bytes
            f.write(gzip.compress(data, compresslevel=9, mtime=0))
        print(f"{path}: json {len(data)} B -> tgs {os.path.getsize(path)} B")


if __name__ == "__main__":
    main()

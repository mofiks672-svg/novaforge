#!/usr/bin/env python3
"""Generates the installer artwork in ./assets (run from any folder: python make-assets.py).
Needs Pillow. Edit colours/text here and rerun instead of editing the BMP/PNG files by hand."""
import os, random
from PIL import Image, ImageDraw, ImageFont, ImageFilter

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
os.makedirs(OUT, exist_ok=True)
BG = (18, 8, 6)

G = {'N': ["#...#", "##..#", "#.#.#", "#..##", "#...#"], 'O': [".###.", "#...#", "#...#", "#...#", ".###."],
     'V': ["#...#", "#...#", "#...#", ".#.#.", "..#.."], 'A': [".###.", "#...#", "#####", "#...#", "#...#"],
     'F': ["#####", "#....", "####.", "#....", "#...."], 'R': ["####.", "#...#", "####.", "#..#.", "#...#"],
     'G': [".####", "#....", "#..##", "#...#", ".####"], 'E': ["#####", "#....", "####.", "#....", "#####"],
     ' ': ["..", "..", "..", "..", ".."]}

def hx(v): return ((v >> 16) & 255, (v >> 8) & 255, v & 255)
THEMES = {  # name: (gradient, accent, hot, ok, text)
    "forge":    ([0xFFE7A0, 0xFFB347, 0xF26B38, 0xD2452F, 0xA63A2C], 0xF5A04A, 0xFFF6D5, 0x9CC069, (18, 8, 6)),
    "amber":    ([0xFFE2A8, 0xFFC145, 0xFFA000, 0xC77700], 0xFFB000, 0xFFF3D6, 0xD7C26B, (16, 10, 2)),
    "phosphor": ([0xD4FFC9, 0x7DFF8A, 0x2FD06A, 0x178F4A], 0x52E37B, 0xF0FFF0, 0x7DFF8A, (4, 14, 8)),
    "steel":    ([0xF2F2F0, 0xC9CCCF, 0x9AA0A6, 0x6E7378], 0xD8D2C4, 0xFFFFFF, 0xA8C090, (14, 15, 17)),
    "default":  ([0xFFFFFF, 0xFFFFFF], 0xC0C0C0, 0xFFFFFF, 0x13A10E, (12, 12, 12)),
    "mono":     ([0xD0D0D0, 0xD0D0D0], 0xD0D0D0, 0xFFFFFF, 0xD0D0D0, (12, 12, 12)),
}
FORGE = [hx(v) for v in THEMES["forge"][0]]

def grad_at(stops, t):
    t = max(0.0, min(1.0, t)); n = len(stops) - 1
    if n == 0: return stops[0]
    i = min(int(t * n), n - 1); f = t * n - i; a, b = stops[i], stops[i + 1]
    return tuple(int(a[k] + (b[k] - a[k]) * f) for k in range(3))

def draw_word(d, word, x0, y0, px, stops, gap=1, shade=0.06):
    total = sum(len(G[c][0]) for c in word) + gap * (len(word) - 1); x = 0
    for c in word:
        g = G[c]
        for yy in range(5):
            for xx in range(len(g[0])):
                if g[yy][xx] == '#':
                    col = grad_at(stops, (x + xx) / max(1, total - 1))
                    col = tuple(int(v * (1 - yy * shade)) for v in col)
                    d.rectangle([x0 + (x + xx) * px, y0 + yy * px, x0 + (x + xx + 1) * px - 1, y0 + (yy + 1) * px - 1], fill=col)
        x += len(g[0]) + gap
    return total * px

def word_w(word, px, gap=1): return (sum(len(G[c][0]) for c in word) + gap * (len(word) - 1)) * px

def glow(W, H, cx, cy, r, color, strength):
    """Soft radial ember glow as an RGB layer to add on top of the background."""
    im = Image.new("RGB", (W, H), (0, 0, 0)); d = ImageDraw.Draw(im)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=tuple(int(c * strength) for c in color))
    return im.filter(ImageFilter.GaussianBlur(r * 0.45))

def add(a, b): return Image.fromarray(__import__("numpy").minimum(__import__("numpy").asarray(a, dtype="int16") + __import__("numpy").asarray(b, dtype="int16"), 255).astype("uint8"))

def sparks(d, W, H, n, y_from, s, seed=7, alpha=1.0):
    rnd = random.Random(seed)
    for _ in range(n):
        x = rnd.random() * W; y = y_from + rnd.random() * (H - y_from); r = rnd.choice([1, 1, 1, 2]) * s
        c = grad_at(FORGE, rnd.random() * 0.6); c = tuple(int(BG[k] + (c[k] - BG[k]) * alpha) for k in range(3))
        d.rectangle([x, y, x + r, y + r], fill=c)

def anvil(d, ax, ay, u):
    body, light, dark = (68, 70, 74), (150, 153, 158), (50, 52, 55)
    d.rectangle([ax - 38 * u, ay - 10 * u, ax + 30 * u, ay], fill=body)
    d.polygon([(ax + 30 * u, ay - 10 * u), (ax + 48 * u, ay - 10 * u), (ax + 30 * u, ay - 2 * u)], fill=body)
    d.rectangle([ax - 38 * u, ay - 10 * u, ax + 30 * u, ay - 8 * u], fill=light)
    d.rectangle([ax - 14 * u, ay, ax + 8 * u, ay + 14 * u], fill=dark)
    d.rectangle([ax - 26 * u, ay + 14 * u, ax + 20 * u, ay + 20 * u], fill=dark)

def font(size, bold=False):
    for f in (["LiberationSans-Bold.ttf", "DejaVuSans-Bold.ttf", "arialbd.ttf"] if bold else ["LiberationSans-Regular.ttf", "DejaVuSans.ttf", "arial.ttf"]):
        try: return ImageFont.truetype(f, size)
        except OSError: pass
    import subprocess
    p = subprocess.run(["fc-match", "-f", "%{file}", "Liberation Sans:bold" if bold else "Liberation Sans"], capture_output=True, text=True).stdout
    return ImageFont.truetype(p, size)

def mono(size):
    import subprocess
    p = subprocess.run(["fc-match", "-f", "%{file}", "Liberation Mono"], capture_output=True, text=True).stdout
    return ImageFont.truetype(p, size)

# ---------- page background (all wizard pages) ----------
def background(W, H):
    im = Image.new("RGB", (W, H), BG)
    im = add(im, glow(W, H, int(W * 0.92), int(H * 1.05), int(H * 0.55), (166, 58, 44), 0.55))
    im = add(im, glow(W, H, int(W * 0.05), int(H * -0.1), int(H * 0.35), (90, 40, 20), 0.25))
    d = ImageDraw.Draw(im)
    sparks(d, W, H, int(W * H / 9000), H * 0.65, max(1, W // 600), seed=3, alpha=0.45)
    return im

for w, h in [(596, 432), (796, 576), (994, 720), (1272, 922)]:
    background(w, h).save(f"{OUT}/back-{w}.png")

# ---------- big image (welcome page; finish page gets its own variant at runtime) ----------
import math
VERSION = "1.5.0"

def vgrad(W, H, top, bottom):
    im = Image.new("RGB", (W, H)); d = ImageDraw.Draw(im)
    for y in range(H):
        t = (y / max(1, H - 1)) ** 1.6
        d.line([(0, y), (W, y)], fill=tuple(int(top[k] + (bottom[k] - top[k]) * t) for k in range(3)))
    return im

def vignette(im, strength=0.55):
    import numpy as np
    W, H = im.size
    yy, xx = np.mgrid[0:H, 0:W]
    r = np.sqrt(((xx - W / 2) / (W * 0.75)) ** 2 + ((yy - H * 0.55) / (H * 0.75)) ** 2)
    k = np.clip(1 - strength * np.clip(r - 0.35, 0, 1) ** 1.5, 0, 1)[..., None]
    return Image.fromarray((np.asarray(im, dtype="float32") * k).astype("uint8"))

def glowing_logo(im, words, cx, y0, px):
    """Pixel logo with a blurred heat halo behind it."""
    W, H = im.size
    layer = Image.new("RGB", (W, H), (0, 0, 0)); d = ImageDraw.Draw(layer)
    for i, wd in enumerate(words):
        tw = word_w(wd, px); draw_word(d, wd, cx - tw // 2, y0 + i * 7 * px, px, FORGE)
    halo = layer.filter(ImageFilter.GaussianBlur(px * 2.2)).point(lambda v: int(v * 0.85))
    im = add(im, halo)
    mask = layer.convert("L").point(lambda v: 255 if v > 0 else 0)
    im.paste(layer, (0, 0), mask)
    return im

def hammer(d, hx_, hy, u, angle):
    """Hammer pivoting around its handle end (hx_, hy); angle in degrees, 0 = head pointing up-left."""
    a = math.radians(angle)
    def rot(x, y): return (hx_ + x * math.cos(a) - y * math.sin(a), hy + x * math.sin(a) + y * math.cos(a))
    handle = [rot(-2 * u, 0), rot(2 * u, 0), rot(2 * u, -46 * u), rot(-2 * u, -46 * u)]
    d.polygon(handle, fill=(110, 72, 46))
    head = [rot(-15 * u, -44 * u), rot(13 * u, -44 * u), rot(13 * u, -56 * u), rot(-15 * u, -56 * u)]
    d.polygon(head, fill=(92, 95, 100))
    d.polygon([rot(-15 * u, -54 * u), rot(13 * u, -54 * u), rot(13 * u, -56 * u), rot(-15 * u, -56 * u)], fill=(170, 174, 180))

def spark_burst(im, cx, cy, s, n, seed, spread=1.0):
    """Sparks flying out of the strike point on short parabolic arcs, with a soft glow."""
    W, H = im.size; rnd = random.Random(seed)
    layer = Image.new("RGB", (W, H), (0, 0, 0)); d = ImageDraw.Draw(layer)
    for _ in range(n):
        ang = math.radians(rnd.uniform(-170, -10)); v = rnd.uniform(18, 70) * s * spread
        steps = rnd.randint(4, 9); x, y = cx, cy
        vx, vy = math.cos(ang) * v / steps, math.sin(ang) * v / steps
        col = grad_at(FORGE, rnd.random() * 0.55)
        for i in range(steps):
            nx, ny = x + vx, y + vy; vy += 0.9 * s
            fade = 1 - i / steps
            d.line([(x, y), (nx, ny)], fill=tuple(int(c * fade) for c in col), width=max(1, int(s)))
            x, y = nx, ny
    im = add(im, layer.filter(ImageFilter.GaussianBlur(2 * s)))
    return add(im, layer)

def ingot(im, cx, cy, u, done=False):
    """Glowing bar on the anvil; when done it is the cooled, finished blade."""
    W, H = im.size
    layer = Image.new("RGB", (W, H), (0, 0, 0)); d = ImageDraw.Draw(layer)
    if done:
        d.polygon([(cx - 30 * u, cy), (cx + 22 * u, cy), (cx + 34 * u, cy - 3 * u), (cx + 22 * u, cy - 6 * u), (cx - 30 * u, cy - 6 * u)], fill=(205, 208, 212))
        d.rectangle([cx - 30 * u, cy - 6 * u, cx + 22 * u, cy - 5 * u], fill=(250, 250, 250))
        d.rectangle([cx - 40 * u, cy - 5 * u, cx - 30 * u, cy - 1 * u], fill=(120, 76, 48))
        return add(im, layer.filter(ImageFilter.GaussianBlur(4 * u)).point(lambda v: int(v * 0.25))).copy() if False else _paste(im, layer)
    d.rounded_rectangle([cx - 22 * u, cy - 6 * u, cx + 22 * u, cy], radius=max(1, int(2 * u)), fill=(255, 196, 92))
    d.rectangle([cx - 16 * u, cy - 5 * u, cx + 16 * u, cy - 3 * u], fill=(255, 246, 213))
    im = add(im, layer.filter(ImageFilter.GaussianBlur(9 * u)))
    return _paste(im, layer)

def _paste(im, layer):
    im = im.copy(); im.paste(layer, (0, 0), layer.convert("L").point(lambda v: 255 if v > 0 else 0)); return im

def check_badge(im, cx, cy, r):
    W, H = im.size
    halo = Image.new("RGB", (W, H), (0, 0, 0)); ImageDraw.Draw(halo).ellipse([cx - r * 1.6, cy - r * 1.6, cx + r * 1.6, cy + r * 1.6], fill=(70, 110, 40))
    im = add(im, halo.filter(ImageFilter.GaussianBlur(r * 0.8)))
    d = ImageDraw.Draw(im)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(122, 170, 78), outline=(190, 225, 140), width=max(1, r // 9))
    w = max(2, r // 4)
    d.line([(cx - r * 0.45, cy + r * 0.02), (cx - r * 0.1, cy + r * 0.38), (cx + r * 0.5, cy - r * 0.35)], fill=(255, 255, 240), width=w, joint="curve")
    return im

def sidebar(W, H, finish=False):
    s = W / 164
    im = vgrad(W, H, (14, 7, 6), (46, 16, 10))
    im = add(im, glow(W, H, W // 2, int(H * 0.86), int(H * 0.30), (210, 80, 40), 0.85))
    im = add(im, glow(W, H, W // 2, int(H * 0.22), int(H * 0.16), (120, 50, 25), 0.35))
    d = ImageDraw.Draw(im)
    sparks(d, W, H, int(40 * s), H * 0.35, max(1, int(s)), seed=11, alpha=0.35)
    px = max(2, int(round(5 * s)))
    top = int(H * 0.12)
    im = glowing_logo(im, ["NOVA", "FORGE"], W // 2, top, px)
    d = ImageDraw.Draw(im)
    f = font(max(9, int(11 * s))); fb = font(max(8, int(9 * s)), bold=True)
    t = "AI-кузница кода"; tw = d.textlength(t, font=f)
    ty = top + 14 * px
    d.text(((W - tw) / 2, ty), t, font=f, fill=(196, 146, 110))
    # thin ember divider + version pill
    ly = ty + int(22 * s)
    for x in range(int(W * 0.2), int(W * 0.8)):
        k = 1 - abs((x - W / 2) / (W * 0.3))
        d.point((x, ly), fill=tuple(int(BG[c] + (FORGE[2][c] - BG[c]) * k) for c in range(3)))
    v = ("v" + VERSION) if not finish else "установлено"
    vw = d.textlength(v, font=fb); pw = vw + 14 * s; py = ly + int(10 * s)
    d.rounded_rectangle([(W - pw) / 2, py, (W + pw) / 2, py + 16 * s], radius=int(8 * s),
                        outline=(122, 170, 78) if finish else (150, 70, 40), fill=(28, 14, 10))
    d.text(((W - vw) / 2, py + 2.5 * s), v, font=fb, fill=(190, 225, 140) if finish else (255, 179, 71))
    u = max(1, s)
    ax, ay = W // 2, int(H * 0.82)
    if finish:
        im = ingot(im, ax - 4 * u, ay - 10 * u, u, done=True)
        im = check_badge(im, ax, int(H * 0.62), int(17 * s))
        d = ImageDraw.Draw(im); anvil(d, ax, ay, u)
        im = ingot(im, ax - 4 * u, ay - 10 * u, u, done=True)
    else:
        d = ImageDraw.Draw(im); anvil(d, ax, ay, u)
        im = ingot(im, ax - 6 * u, ay - 10 * u, u)
        im = spark_burst(im, ax + 2 * u, ay - 17 * u, s, int(46 * s ** 0.5) + 20, seed=5)
        d = ImageDraw.Draw(im); hammer(d, ax + 50 * u, ay - 6 * u, u, -48)
    return vignette(im, 0.6)

WIZ = [(164, 314), (192, 386), (246, 459), (273, 556), (328, 628), (355, 700), (410, 797)]
for w, h in WIZ:
    sidebar(w, h).save(f"{OUT}/wizard-{w}.bmp")
    sidebar(w, h, finish=True).save(f"{OUT}/finish-{w}.bmp")

# ---------- small image (top right of inner pages) ----------
def small(W, H):
    S = 4  # supersample for smooth edges
    im = Image.new("RGB", (W * S, H * S), BG)
    s = min(W, H) * S
    cx, cy = W * S // 2, H * S // 2
    im = add(im, glow(W * S, H * S, cx, cy, int(s * 0.30), (200, 80, 40), 0.40))
    d = ImageDraw.Draw(im)
    r = int(s * 0.40)
    px = max(1, int(r * 1.25 / 5)); tw = 5 * px
    draw_word(d, "N", cx - tw // 2, cy - tw // 2, px, FORGE)
    return im.resize((W, H), Image.LANCZOS)

for w, h in [(55, 58), (64, 68), (69, 73), (83, 80), (92, 97), (110, 106), (119, 123), (138, 140)]:
    small(w, h).save(f"{OUT}/small-{w}.bmp")

# ---------- theme previews (Appearance page) ----------
def preview(name, W=300, H=220):
    stops, accent, hot, ok, bg = THEMES[name]; stops = [hx(v) for v in stops]; accent, hot, ok = hx(accent), hx(hot), hx(ok)
    plain = name in ("default", "mono")
    text = (225, 220, 215) if not plain else (220, 220, 220)
    muted = (140, 132, 126) if name != "mono" else (150, 150, 150)
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, W - 1, H - 1], radius=9, fill=bg, outline=(78, 66, 60))
    d.rounded_rectangle([0, 0, W - 1, 22], radius=9, fill=(42, 35, 32)); d.rectangle([1, 12, W - 2, 22], fill=(42, 35, 32))
    for i, c in enumerate([(232, 90, 80), (240, 190, 70), (110, 190, 90)]): d.ellipse([10 + i * 15, 7, 19 + i * 15, 16], fill=c)
    fm, fs = mono(11), mono(10)
    d.text((W / 2 - 24, 5), "nova.exe", font=fm, fill=(170, 160, 150))
    px = 3; tw = word_w("NOVA FORGE", px); draw_word(d, "NOVA FORGE", (W - tw) // 2, 36, px, stops)
    y = 36 + 5 * px + 8
    d.text(((W - tw) // 2, y), "AI-кузница кода", font=fs, fill=muted)
    d.text(((W + tw) // 2 - 36, y), "v1.5.0", font=fs, fill=accent)
    y += 24
    d.text((14, y), "●", font=fm, fill=ok if not plain else text); d.text((28, y), "Read(src/main.cpp)", font=fm, fill=text)
    y += 16; d.text((28, y), "└ 1503 строки", font=fm, fill=muted)
    y += 18
    d.text((14, y), "●", font=fm, fill=ok if not plain else text); d.text((28, y), "Edit(src/parser.cpp)", font=fm, fill=text)
    y += 16; d.text((28, y), "└ +12 −3", font=fm, fill=muted)
    y += 22
    d.rounded_rectangle([10, y, W - 11, y + 26], radius=5, outline=accent if name != "mono" else (160, 160, 160))
    d.text((18, y + 6), ">", font=fm, fill=accent); d.text((32, y + 6), "добавь тесты для парсера", font=fm, fill=hot if not plain else text)
    return im

for n in THEMES: preview(n).save(f"{OUT}/theme-{n}.bmp")

# ---------- icon (nova.exe, Setup, shortcuts) ----------
def icon(S):
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius=max(2, S // 6), fill=BG + (255,))
    px = max(1, int(S * 0.7 / 5)); tw = 5 * px; draw_word(d, "N", (S - tw) // 2, (S - tw) // 2, px, FORGE)
    return im

icon(256).save(f"{OUT}/novaforge.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("assets written to", OUT)

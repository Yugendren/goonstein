#!/usr/bin/env python3
"""Generate a hand-painted wooden sign board texture.

A weathered plank background (a few horizontal seams, grain streaks, a couple of nail heads)
with a large centred word painted on in flaking off-white/cream, a smaller second line under
it, and a little paint wear and a couple of drips. Deterministic (seeded).

*** CRITICAL: the image is written UPSIDE DOWN -- vertically flipped as the very last step
before saving. *** Everything above is drawn right-side up, in normal image coordinates, and
then the whole thing is flipped top-to-bottom right before `save()`. This is not a bug: the
game paints this texture onto a level `block` using its world-planar mapping, which for a
face whose normal is +/-X gives uv = (world_z, world_y) -- v grows with world Y (upward),
while texture v = 0 is the image's TOP row. An un-flipped image would therefore appear
vertically mirrored once painted onto the board, so flipping it here is what makes the sign
read the right way up in the world. Horizontal is already correct (looking along +X,
screen-right is +Z, and u grows with z), so this script does NOT mirror horizontally.

The block this paints onto maps exactly one copy of the texture across a 1m x 1m face
(see assets/levels/island.txt, the `sign_culvert` block), so the word has to sit entirely
inside the image with a margin of at least 12px all round -- there is no tiling to hide a
word that runs off the edge.

Everything on the sign is fictional.

Usage:
    python3 tools/make_sign.py                       # writes assets/textures/sign_culvert.png
    python3 tools/make_sign.py --text "CULVERT" --sub "OUTFALL  NO ENTRY" --out assets/textures/sign_culvert.png
"""
import argparse
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFont

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
DEFAULT_OUT = os.path.join(REPO_ROOT, "assets", "textures", "sign_culvert.png")
FONT_PATH = os.path.join(REPO_ROOT, "assets", "fonts", "VT323-Regular.ttf")

SIZE = 256
MARGIN = 12
MAX_SPAN = SIZE - 2 * MARGIN     # 232, the box both lines have to fit inside together

# Weathered wood, warm and a little grey with age.
WOOD_BASE = (134, 92, 56)
PLANK_JITTER = 12                # per-plank base colour variance
SEAM_DARK = (56, 36, 20)
SEAM_LIGHT = (176, 130, 82)
NAIL_DARK = (44, 40, 38)
NAIL_LIGHT = (150, 140, 128)
PAINT_COLOR = (230, 221, 198)     # flaking off-white / cream


def load_font(size):
    """VT323 at `size`, falling back to the PIL default bitmap font if the ttf is missing."""
    if os.path.exists(FONT_PATH):
        try:
            return ImageFont.truetype(FONT_PATH, size)
        except OSError:
            pass
    try:
        return ImageFont.load_default(size=size)   # Pillow >= 9.2
    except TypeError:
        return ImageFont.load_default()


def text_box(font, text):
    l, t, r, b = font.getbbox(text)
    return r - l, b - t, l, t


def fit_two_lines(main_text, sub_text):
    """Shrink both lines together until each fits MAX_SPAN wide and the pair, stacked with a
    gap, fits MAX_SPAN tall -- the block only ever shows one copy of this texture, so there is
    nowhere for an oversized word to spill to."""
    main_size, sub_size = 120, 54
    gap = 10
    while True:
        main_font = load_font(main_size)
        sub_font = load_font(sub_size)
        mw, mh, ml, mt = text_box(main_font, main_text)
        sw, sh, sl, st = text_box(sub_font, sub_text) if sub_text else (0, 0, 0, 0)
        total_h = mh + (gap + sh if sub_text else 0)
        if mw <= MAX_SPAN and sw <= MAX_SPAN and total_h <= MAX_SPAN:
            return (main_font, mw, mh, ml, mt), (sub_font, sw, sh, sl, st), gap
        if main_size <= 28 and sub_size <= 14:
            return (main_font, mw, mh, ml, mt), (sub_font, sw, sh, sl, st), gap
        main_size = max(28, main_size - 2)
        sub_size = max(14, sub_size - 1)


def make_wood(rng):
    """The plank background: a few horizontal boards with grain streaks, beveled seams and a
    couple of nail heads, all as opaque colour -- no alpha here, it is the base of the board."""
    im = Image.new("RGB", (SIZE, SIZE), WOOD_BASE)
    d = ImageDraw.Draw(im)

    plank_count = rng.choice((4, 5))
    plank_h = SIZE / plank_count
    for i in range(plank_count):
        y0, y1 = i * plank_h, (i + 1) * plank_h
        base = tuple(max(0, min(255, c + rng.randint(-PLANK_JITTER, PLANK_JITTER))) for c in WOOD_BASE)
        d.rectangle([0, y0, SIZE, y1], fill=base)
        # grain: short horizontal streaks, lighter or darker than the plank's own base
        for _ in range(46):
            gy = rng.uniform(y0 + 2, y1 - 2)
            gx0 = rng.uniform(-10, SIZE * 0.55)
            glen = rng.uniform(24, 150)
            drift = rng.choice((-1, 1)) * rng.randint(6, 22)
            streak = tuple(max(0, min(255, c + drift)) for c in base)
            d.line([(gx0, gy), (gx0 + glen, gy + rng.uniform(-0.6, 0.6))], fill=streak, width=1)

    # seams between planks: a dark line with a lighter highlight just below it (the bevel)
    for i in range(1, plank_count):
        sy = i * plank_h
        d.line([(0, sy), (SIZE, sy)], fill=SEAM_DARK, width=1)
        d.line([(0, sy + 1), (SIZE, sy + 1)], fill=SEAM_LIGHT, width=1)

    # a couple of nail heads, sitting on seams near the board edges
    nail_seams = [1, plank_count - 1] if plank_count > 2 else [1]
    nail_xs = (22, SIZE - 22)
    for si in nail_seams[:2]:
        ny = si * plank_h
        nx = nail_xs[si % 2]
        r = 2.6
        d.ellipse([nx - r, ny - r, nx + r, ny + r], fill=NAIL_DARK)
        d.ellipse([nx - r + 1, ny - r + 1, nx - r + 2.4, ny - r + 2.4], fill=NAIL_LIGHT)

    return im


def make_paint_mask(rng, main, sub, main_font_info, sub_font_info, gap):
    """A greyscale mask: 255 where the letters are painted, faded by wear. Returns the mask and
    the paint's own (slightly varied) colour layer, both to be alpha-composited over the wood."""
    mask = Image.new("L", (SIZE, SIZE), 0)
    dm = ImageDraw.Draw(mask)

    main_font, mw, mh, ml, mt = main_font_info
    sub_font, sw, sh, sl, st = sub_font_info
    total_h = mh + (gap + sh if sub else 0)
    top = (SIZE - total_h) / 2.0

    main_x = SIZE / 2.0 - mw / 2.0 - ml
    main_y = top - mt
    dm.text((main_x, main_y), main, font=main_font, fill=255)

    if sub:
        sub_top = top + mh + gap
        sub_x = SIZE / 2.0 - sw / 2.0 - sl
        sub_y = sub_top - st
        dm.text((sub_x, sub_y), sub, font=sub_font, fill=255)

    # wear: a scatter of soft blobs that knock the paint's opacity down unevenly -- concentrated
    # a little more over the middle of the board, which is where a hand would rub the sign
    wear = Image.new("L", (SIZE, SIZE), 255)
    dw = ImageDraw.Draw(wear)
    for _ in range(70):
        cx = rng.uniform(0, SIZE)
        cy = rng.gauss(SIZE / 2.0, SIZE * 0.30)
        r = rng.uniform(2.5, 9.0)
        v = rng.randint(30, 190)
        dw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=v)

    flaked = ImageChops.multiply(mask, wear)

    # the paint colour itself, with a little per-pixel brightness jitter so it is not one flat
    # cream swatch
    paint = Image.new("RGB", (SIZE, SIZE), PAINT_COLOR)
    jitter = [max(0, min(255, PAINT_COLOR[0] + rng.randint(-8, 8))) for _ in range(SIZE * SIZE)]
    r_band = Image.new("L", (SIZE, SIZE))
    r_band.putdata(jitter)
    paint = Image.merge("RGB", (r_band,
                                 Image.eval(r_band, lambda v: max(0, min(255, v - (PAINT_COLOR[0] - PAINT_COLOR[1])))),
                                 Image.eval(r_band, lambda v: max(0, min(255, v - (PAINT_COLOR[0] - PAINT_COLOR[2]))))))

    return flaked, paint, (main_x - ml, main_y + mt, mw, mh)


def add_drips(board, rng, text_rect):
    """A couple of paint drips trailing down from the lettering, pre-blended against the wood
    since we are drawing on a plain RGB canvas rather than compositing real per-pixel alpha."""
    d = ImageDraw.Draw(board)
    x0, y0, w, h = text_rect
    baseline = y0 + h
    for _ in range(rng.randint(2, 4)):
        dx = x0 + rng.uniform(0.15, 0.85) * w
        length = rng.uniform(10, 34)
        steps = int(length)
        for s in range(steps):
            t = s / max(1, steps - 1)
            y = baseline + t * length
            alpha = (1.0 - t) * rng.uniform(0.35, 0.55)
            wood = board.getpixel((int(dx), int(min(SIZE - 1, y))))
            blended = tuple(int(wood[c] + (PAINT_COLOR[c] - wood[c]) * alpha) for c in range(3))
            d.line([(dx - 0.5, y), (dx + 0.5, y)], fill=blended)


def make_sign(text, sub, seed):
    rng = random.Random(seed)

    board = make_wood(rng)

    main_info, sub_info, gap = fit_two_lines(text, sub)
    flaked_mask, paint_rgb, text_rect = make_paint_mask(rng, text, sub, main_info, sub_info, gap)

    paint_rgba = Image.merge("RGBA", (*paint_rgb.split(), flaked_mask))
    board = board.convert("RGBA")
    board = Image.alpha_composite(board, paint_rgba)

    board = board.convert("RGB")
    add_drips(board, rng, text_rect)
    board = board.convert("RGBA")

    # *** the flip: see the module docstring for why this has to happen, last, right here ***
    board = board.transpose(Image.FLIP_TOP_BOTTOM)
    return board


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--text", default="CULVERT")
    ap.add_argument("--sub", default="OUTFALL  NO ENTRY")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", default=DEFAULT_OUT)
    args = ap.parse_args()

    im = make_sign(args.text, args.sub, args.seed)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    im.save(args.out)
    print(f"wrote {args.out} ({im.width}x{im.height}, flipped)")


if __name__ == "__main__":
    main()

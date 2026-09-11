#!/usr/bin/env python3
"""Generate `assets/textures/frond.png`, an RGBA cut-out of one whole coconut-palm frond seen
flat from above, plus a half-size copy `assets/textures/frond_512.png`.

Geometry contract shared with the mesh generator: the rib runs horizontally from u=0 (the base,
where the frond joins the crown) to u=1 (the tip), sitting exactly on v=0.5. Leaflets hang off
both sides of the rib -- the top half of the image is one wing, the bottom half the other. How far
a leaflet may reach from the rib, as a fraction of the image half-height, is given by `env(t)`
below; nothing may be drawn outside that band except the rib itself.

Usage:
    python3 tools/palm/make_frond.py
    python3 tools/palm/make_frond.py --seed 3 --size 1024 256 --leaflets 90 --preview out.png
"""
import argparse
import os

import numpy as np
from PIL import Image, ImageDraw

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_OUT = os.path.join(REPO_ROOT, "assets", "textures", "frond.png")
DEFAULT_OUT_HALF = os.path.join(REPO_ROOT, "assets", "textures", "frond_512.png")

SS = 4  # supersample factor: everything is drawn 4x and box-downsampled for antialiasing

BASE_GREEN = np.array([77.0, 116.0, 46.0])
YELLOW_TIP = np.array([156.0, 155.0, 58.0])
AO_GREEN = np.array([52.0, 82.0, 34.0])
RIB_COLOR = np.array([176.0, 182.0, 118.0])


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def env(t):
    """The leaflet envelope: how far leaflets may reach from the rib, as a fraction of the
    image's half height. Shared contract with the mesh generator -- must match exactly."""
    a = smoothstep(0.14, 0.40, t)
    b = 1.0 - smoothstep(0.70, 1.00, t)
    return 0.04 + 0.96 * (a * b) ** 0.65


# ---------------------------------------------------------------------------------------------
# Pull-push flood fill: guarantees every texel, however far from any opaque pixel, ends up with
# a sensible colour, by building a mip pyramid of alpha-weighted colour (the "pull" -- coarser
# levels are just weighted averages of their children, which a box downsample already computes
# for free) and then walking back down from the coarsest level to the finest (the "push"),
# handing any pixel that has no colour of its own the filled value of its parent. A handful of
# dilation iterations only reaches a handful of pixels; this reaches every pixel in O(log2(size))
# passes, which matters here because whole columns near the petiole and the very tip are almost
# entirely transparent and need colour pulled in from far away.
# ---------------------------------------------------------------------------------------------
def pull_push(rgb, alpha):
    h, w = alpha.shape
    w0 = alpha / 255.0
    c_levels = [rgb * w0[..., None]]
    w_levels = [w0]
    shapes = [(h, w)]
    ch, cw = c_levels[0], w_levels[0]
    while ch.shape[0] > 1 or ch.shape[1] > 1:
        hh, ww = ch.shape[0], ch.shape[1]
        ph, pw = hh + (hh % 2), ww + (ww % 2)
        if ph != hh or pw != ww:
            ch = np.pad(ch, ((0, ph - hh), (0, pw - ww), (0, 0)), mode="edge")
            cw = np.pad(cw, ((0, ph - hh), (0, pw - ww)), mode="edge")
        h2, w2 = ph // 2, pw // 2
        ch = ch.reshape(h2, 2, w2, 2, 3).sum(axis=(1, 3))
        cw = cw.reshape(h2, 2, w2, 2).sum(axis=(1, 3))
        c_levels.append(ch)
        w_levels.append(cw)
        shapes.append((h2, w2))

    top_w = w_levels[-1]
    filled = np.where(top_w[..., None] > 1e-9, c_levels[-1] / np.maximum(top_w[..., None], 1e-9), 0.0)
    for k in range(len(c_levels) - 2, -1, -1):
        hh, ww = shapes[k]
        up = np.repeat(np.repeat(filled, 2, axis=0), 2, axis=1)[:hh, :ww, :]
        wk = w_levels[k]
        local_avg = np.where(wk[..., None] > 1e-9, c_levels[k] / np.maximum(wk[..., None], 1e-9), 0.0)
        filled = np.where(wk[..., None] > 1e-9, local_avg, up)
    return filled


def _paint_polygon(color_acc, alpha_acc, poly_pts, color_fn):
    """Rasterise one filled polygon into a small cropped buffer (its own bounding box) and
    splat the result into the full-resolution accumulators. Colour is computed per-pixel by
    `color_fn(gx, gy)` rather than being a single flat fill, so a leaflet can carry an AO band,
    a yellow tip and a midrib highlight all baked into its own polygon."""
    hh, ww = alpha_acc.shape
    xs = [p[0] for p in poly_pts]
    ys = [p[1] for p in poly_pts]
    x0 = max(0, int(np.floor(min(xs))) - 1)
    x1 = min(ww, int(np.ceil(max(xs))) + 2)
    y0 = max(0, int(np.floor(min(ys))) - 1)
    y1 = min(hh, int(np.ceil(max(ys))) + 2)
    if x1 <= x0 or y1 <= y0:
        return
    local_pts = [(x - x0, y - y0) for x, y in poly_pts]
    mask_img = Image.new("L", (x1 - x0, y1 - y0), 0)
    ImageDraw.Draw(mask_img).polygon(local_pts, fill=255)
    mask = np.asarray(mask_img, dtype=bool)
    if not mask.any():
        return
    yy, xx = np.nonzero(mask)
    gx = (xx + x0).astype(np.float64)
    gy = (yy + y0).astype(np.float64)
    rgb = color_fn(gx, gy)
    alpha_acc[yy + y0, xx + x0] = 255.0
    color_acc[yy + y0, xx + x0] = rgb


def _leaflet_polygon_and_color(root, D, N, length, root_w, blunt, side_yellow_local, t, brightness_mult, midrib, rng):
    """Build the curved, tapering blade polygon for one leaflet and its per-pixel colour
    function. `D` is the unit direction the leaflet points away from the rib (already tilted
    toward the frond's tip), `N` its perpendicular (used for width)."""
    K = 12
    s_vals = np.linspace(0.0, 1.0, K)
    curve_amount = length * 0.22  # quadratic droop toward the tip along the blade's own length
    tip_w = root_w * 0.30 if blunt else 0.0
    # Real pinnae hold close to their root width for most of their length and only narrow right
    # at the tip -- a full linear taper from root to point wastes most of a leaflet's length on
    # near-zero width and cannot cover its share of the envelope, however densely packed.
    taper_start = 0.94

    left_pts = []
    right_pts = []
    for s in s_vals:
        if s <= taper_start:
            w = root_w
        else:
            k = (s - taper_start) / (1.0 - taper_start)
            w = root_w * (1.0 - k) + tip_w * k
        cx = root[0] + D[0] * (s * length) + curve_amount * s * s
        cy = root[1] + D[1] * (s * length)
        left_pts.append((cx + N[0] * w * 0.5, cy + N[1] * w * 0.5))
        right_pts.append((cx - N[0] * w * 0.5, cy - N[1] * w * 0.5))
    poly = left_pts + right_pts[::-1]

    root_x, root_y = root
    Dx, Dy = D
    Nx, Ny = N
    per_leaflet_var = rng.uniform(0.88, 1.12)
    base_col = BASE_GREEN * per_leaflet_var

    def color_fn(gx, gy):
        px = gx - root_x
        py = gy - root_y
        s = np.clip((px * Dx + py * Dy) / max(length, 1e-6), 0.0, 1.0)
        perp = px * Nx + py * Ny
        local_yellow = smoothstep(0.65, 1.0, s)
        yellowblend = 1.0 - (1.0 - local_yellow) * (1.0 - side_yellow_local)
        col = base_col[None, :] * (1.0 - yellowblend)[:, None] + YELLOW_TIP[None, :] * yellowblend[:, None]
        ao = 1.0 - smoothstep(0.0, 0.12, s)
        col = col * (1.0 - ao * 0.55)[:, None] + AO_GREEN[None, :] * (ao * 0.55)[:, None]
        col = col * brightness_mult
        if midrib:
            near = np.abs(perp) < (SS * 0.55)
            col = np.where(near[:, None], col * 1.35, col)
        return np.clip(col, 6.0, 255.0)

    return poly, color_fn


def make_frond(size, seed, leaflets_per_side):
    W, H = size
    Wss, Hss = W * SS, H * SS
    rng = np.random.default_rng(seed)

    color_acc = np.zeros((Hss, Wss, 3), dtype=np.float64)
    alpha_acc = np.zeros((Hss, Wss), dtype=np.float64)

    half_h_ss = Hss / 2.0

    def build_side(side_sign):
        t_vals = np.sort(rng.uniform(0.0, 1.0, leaflets_per_side))
        # nudge toward an even spread along the rib, then re-jitter -- avoids both a perfectly
        # even comb and a clumpy purely-random scatter.
        idx = np.arange(leaflets_per_side)
        even = (idx + 0.5) / leaflets_per_side
        t_vals = 0.55 * even + 0.45 * t_vals
        t_vals = np.clip(t_vals + rng.normal(0.0, 0.006, leaflets_per_side), 0.0, 1.0)
        t_vals.sort()

        for i, t in enumerate(t_vals):
            roll = rng.random()
            if roll < 0.07:
                continue  # missing entirely -- wind-torn frond

            noise_deg = rng.uniform(-6.0, 6.0)
            theta_deg = np.clip(62.0 - 36.0 * t + noise_deg, 18.0, 89.0)
            theta = np.radians(theta_deg)
            Dx = np.cos(theta)
            Dy = side_sign * np.sin(theta)
            D = (Dx, Dy)
            N = (-Dy, Dx)

            # `full_len` is the length of the polygon's own centreline, not its reach away from
            # the rib -- since the leaflet is tilted toward the tip rather than perpendicular to
            # the rib, only sin(theta) of its length is radial. Size it so the radial reach
            # itself (not the tilted centreline) matches env(t) -- otherwise shallow tip angles
            # (theta near 26 degrees, sin ~0.44) leave the outer half of the envelope empty.
            e = env(t)
            radial_reach = e * half_h_ss * rng.uniform(0.86, 1.0)
            full_len = radial_reach / max(np.sin(theta), 0.3)
            blunt = False
            if roll < 0.17:  # the next 10% (0.07..0.17) are cut short with a blunt tip
                full_len *= rng.uniform(0.45, 0.70)
                blunt = True

            root = (t * Wss, half_h_ss)
            root_w = rng.uniform(6.5, 7.0) * SS

            # Directional shading baked into the colour: derived from this leaflet's own angle
            # noise (rather than from t) so neighbours -- which sit at nearly the same t but get
            # independent noise draws -- read as visibly different brightnesses under flat light.
            frac = np.clip((noise_deg + 6.0) / 12.0, 0.0, 1.0)
            brightness_mult = 0.80 + 0.38 * frac

            side_yellow_local = smoothstep(0.66, 1.0, t)
            midrib = (i % 3) == 0

            poly, color_fn = _leaflet_polygon_and_color(
                root, D, N, full_len, root_w, blunt, side_yellow_local, t, brightness_mult, midrib, rng
            )
            _paint_polygon(color_acc, alpha_acc, poly, color_fn)

    build_side(-1.0)  # top wing: v decreases away from the rib
    build_side(1.0)  # bottom wing: v increases away from the rib

    # Rib last, on top: a tapering spine, opaque along its whole length including the bare
    # petiole section (t < 0.14).
    s_vals = np.linspace(0.0, 1.0, 64)
    rib_left = []
    rib_right = []
    for s in s_vals:
        rw = (7.0 * (1.0 - s) + 1.5 * s) * SS
        cx = s * Wss
        rib_left.append((cx, half_h_ss - rw * 0.5))
        rib_right.append((cx, half_h_ss + rw * 0.5))
    rib_poly = rib_left + rib_right[::-1]

    def rib_color_fn(gx, gy):
        return np.tile(RIB_COLOR, (gx.shape[0], 1))

    _paint_polygon(color_acc, alpha_acc, rib_poly, rib_color_fn)

    # Premultiplied box downsample: averaging colour and alpha independently would smear black
    # (wherever alpha_acc==0, color_acc is still 0) straight into the edge of every leaflet, so
    # colour must be premultiplied by alpha before downsampling and unpremultiplied after.
    premult = color_acc * (alpha_acc / 255.0)[:, :, None]
    premult_d = premult.reshape(H, SS, W, SS, 3).mean(axis=(1, 3))
    alpha_d = alpha_acc.reshape(H, SS, W, SS).mean(axis=(1, 3))
    with np.errstate(invalid="ignore", divide="ignore"):
        rgb_d = np.where(alpha_d[:, :, None] > 1e-6, premult_d / np.maximum(alpha_d[:, :, None] / 255.0, 1e-9), 0.0)

    # Envelope mask at final resolution, for the coverage measurement.
    xs = np.arange(W)
    t_col = xs / max(W - 1, 1)
    env_col = env(t_col)
    ys = np.arange(H)
    v = (ys[:, None] + 0.5) / H
    envelope_mask = np.abs(v - 0.5) <= (0.5 * env_col[None, :])

    coverage = float(np.mean(alpha_d[envelope_mask] / 255.0))

    # Flood colour outward into fully-transparent texels so nothing ever samples black. Plain
    # pull-push is locality-preserving -- a transparent texel a couple of pixels from the bright
    # rib inherits the rib's colour before it ever blends with the much larger, darker population
    # of leaflet green -- so texels deep in the background (alpha far below the antialiased edge
    # band) get pulled the rest of the way toward the whole frond's alpha-weighted mean colour.
    # Texels still inside the AA edge band (alpha up to ~40) are left on their local pull-push
    # value, since that is the correct colour for bilinear sampling right at the cut-out's edge.
    filled_rgb = pull_push(rgb_d, alpha_d)
    global_mean = np.average(rgb_d.reshape(-1, 3), axis=0, weights=alpha_d.reshape(-1) + 1e-9)
    deep_bg = 1.0 - smoothstep(0.0, 40.0, alpha_d)
    filled_rgb = filled_rgb * (1.0 - deep_bg[:, :, None] * 0.85) + global_mean[None, None, :] * (
        deep_bg[:, :, None] * 0.85
    )
    filled_rgb = np.clip(filled_rgb, 6.0, 255.0)

    out = np.zeros((H, W, 4), dtype=np.uint8)
    out[:, :, :3] = np.clip(np.round(filled_rgb), 0, 255).astype(np.uint8)
    out[:, :, 3] = np.clip(np.round(alpha_d), 0, 255).astype(np.uint8)

    low_mask = out[:, :, 3] < 8
    high_mask = out[:, :, 3] > 200
    low_mean = out[:, :, :3][low_mask].astype(np.float64).mean(axis=0) if low_mask.any() else np.zeros(3)
    high_mean = out[:, :, :3][high_mask].astype(np.float64).mean(axis=0) if high_mask.any() else np.zeros(3)

    stats = {
        "env_max": float(np.max(env(np.linspace(0.0, 1.0, 4096)))),
        "coverage": coverage,
        "low_mean": low_mean,
        "high_mean": high_mean,
    }
    return out, stats


def make_preview(rgba, path):
    h, w = rgba.shape[:2]
    tile = 16
    yy, xx = np.meshgrid(np.arange(h) // tile, np.arange(w) // tile, indexing="ij")
    checker = np.where((xx + yy) % 2 == 0, 96, 150).astype(np.uint8)
    bg = np.repeat(checker[:, :, None], 3, axis=2).astype(np.float64)

    fg = rgba[:, :, :3].astype(np.float64)
    a = (rgba[:, :, 3:4].astype(np.float64)) / 255.0
    composited = fg * a + bg * (1.0 - a)
    composited = np.clip(np.round(composited), 0, 255).astype(np.uint8)

    alpha_rgb = np.repeat(rgba[:, :, 3:4], 3, axis=2)

    gap = np.full((h, 8, 3), 255, dtype=np.uint8)
    combo = np.concatenate([composited, gap, alpha_rgb], axis=1)
    Image.fromarray(combo, mode="RGB").save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--size", type=int, nargs=2, default=[1024, 256], metavar=("W", "H"))
    ap.add_argument("--leaflets", type=int, default=200, help="leaflets per side")
    ap.add_argument("--preview", default=None)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--out-half", default=DEFAULT_OUT_HALF)
    args = ap.parse_args()

    W, H = args.size
    rgba, stats = make_frond((W, H), args.seed, args.leaflets)

    print(f"env(t) max over t in [0,1]: {stats['env_max']:.4f}")
    print(f"measured opaque coverage inside envelope: {stats['coverage']:.4f} (target 0.72..0.88)")
    low, high = stats["low_mean"], stats["high_mean"]
    print(f"mean RGB alpha<8:   {low[0]:.1f},{low[1]:.1f},{low[2]:.1f}")
    print(f"mean RGB alpha>200: {high[0]:.1f},{high[1]:.1f},{high[2]:.1f}")
    denom = np.maximum(high, 1e-6)
    rel_diff = np.max(np.abs(low - high) / denom)
    print(f"max relative channel difference: {rel_diff * 100:.1f}% (must be within 25%)")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    img = Image.fromarray(rgba, mode="RGBA")
    img.save(args.out)
    print(f"wrote {args.out} ({W}x{H})")

    # A generic resample (Lanczos included) rings at the hard alpha edge of a cut-out, which can
    # overshoot both colour and alpha to 0 right next to the leaf and reintroduce the pure-black,
    # fully-transparent texels the pull-push flood above exists to prevent. A plain premultiplied
    # 2x2 box average has no overshoot and is exact for an even half-size reduction.
    src_rgb = rgba[:, :, :3].astype(np.float64)
    src_alpha = rgba[:, :, 3].astype(np.float64)
    alpha_sum = src_alpha.reshape(H // 2, 2, W // 2, 2).sum(axis=(1, 3))  # 0..1020 over the 2x2 block
    half_alpha = alpha_sum / 4.0
    csum = (src_rgb * src_alpha[:, :, None]).reshape(H // 2, 2, W // 2, 2, 3).sum(axis=(1, 3))  # rgb-weighted sum
    # Source RGB is never black even where alpha is 0 (the pull-push fill above already gave it a
    # sensible colour), so a fully-transparent 2x2 block still averages to a sensible colour here.
    plain_avg = src_rgb.reshape(H // 2, 2, W // 2, 2, 3).mean(axis=(1, 3))
    with np.errstate(invalid="ignore", divide="ignore"):
        premult_avg = csum / np.maximum(alpha_sum[:, :, None], 1e-9)
    half_rgb = np.where(alpha_sum[:, :, None] > 1e-6, premult_avg, plain_avg)
    half_rgb = np.clip(half_rgb, 6.0, 255.0)
    half_arr = np.zeros((H // 2, W // 2, 4), dtype=np.uint8)
    half_arr[:, :, :3] = np.clip(np.round(half_rgb), 0, 255).astype(np.uint8)
    half_arr[:, :, 3] = np.clip(np.round(half_alpha), 0, 255).astype(np.uint8)
    half = Image.fromarray(half_arr, mode="RGBA")
    os.makedirs(os.path.dirname(args.out_half), exist_ok=True)
    half.save(args.out_half)
    print(f"wrote {args.out_half} ({W // 2}x{H // 2})")

    if args.preview:
        os.makedirs(os.path.dirname(args.preview), exist_ok=True)
        make_preview(rgba, args.preview)
        print(f"wrote preview {args.preview}")


if __name__ == "__main__":
    main()

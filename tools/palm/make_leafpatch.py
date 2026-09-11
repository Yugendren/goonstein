#!/usr/bin/env python3
"""Generate `assets/textures/leafpatch.png`, a 2x2 atlas of four different leaf-clump cut-outs
used by the cheap card-based bushes (`tools/palm/make_bush.py`).

Atlas layout, a 2x2 grid of `size/2`-square patches (row 0 on top, matching image rows):

    patch 0 (top-left)      patch 1 (top-right)
    patch 2 (bottom-left)   patch 3 (bottom-right)

patch index = row*2 + col. Patches 0 and 3 are sea grape (broad round leaves); patch 1 is bay
cedar (narrow lance leaves in whorls); patch 2 is a sparse clump for the outside of a bush, so a
cluster of cards doesn't read as a solid ball.

Each patch is its own little clump of leaves growing from a stem cluster at the bottom centre of
its own 512x512 square, fanning up and out. A twig skeleton (hair-thin, dark, nearly invisible --
see TWIG_W_MIN/MAX and TWIG_COLOR) fans out from that cluster, and every twig carries 3-6 leaves
spread along its outer 65%, at varied distances, with one always at or past the twig's own tip so
no bare stick end ever shows. On top of that, about a third of each patch's leaves belong to no
twig at all: they are scattered near the clump's centre so the middle reads as a dense mass and
only the fringe is where daylight gets through. `tools/palm/make_bush.py` samples one 0.5x0.5 UV
sub-rectangle of this atlas per card, sometimes mirrored -- so every patch must carry a fully
transparent margin on all four of its own edges (bilinear filtering would otherwise drag one
patch's leaves across the seam into its neighbour), and colour still has to be flooded out to the
margin so a card's edge never samples black.

Usage:
    python3 tools/palm/make_leafpatch.py
    python3 tools/palm/make_leafpatch.py --seed 3 --size 1024 --preview out.png
"""
import argparse
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_OUT = os.path.join(REPO_ROOT, "assets", "textures", "leafpatch.png")

SS = 4  # supersample factor: everything is drawn 4x and box-downsampled for antialiasing
MARGIN_PX = 12  # transparent margin kept on every edge of every patch, at final resolution (>= 8)

GREEN_LO = np.array([60.0, 95.0, 45.0])
GREEN_HI = np.array([112.0, 142.0, 62.0])
BRONZE = np.array([126.0, 84.0, 52.0])
TWIG_COLOR = np.array([74.0, 64.0, 50.0])  # dark and desaturated -- twigs must read as shadow
                                            # between leaves, never as branches
LIGHT_DIR_DEG = -32.0  # light arrives from the upper right; leaves facing it read brighter

TWIG_W_MIN = 1.0  # twig width at final resolution, at its root end (tapers to 0.45x at its tip)
TWIG_W_MAX = 1.7  # -- must stay well under the 2px ceiling so twigs nearly vanish
DILATE_PX = 6  # "no bare twig tip" check: a twig texel counts as covered if a leaf texel sits
               # within this many final-resolution px of it
FILLER_FRACTION = 0.35  # target fraction of each patch's total leaf count that belongs to no twig,
                         # scattered near the clump's centre instead


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def random_green(rng):
    return lerp(GREEN_LO, GREEN_HI, rng.uniform(0.0, 1.0))


def bay_cedar_green(rng):
    """Bay cedar reads lighter and greyer than sea grape -- blend a leaf green toward its own
    grey and nudge it slightly cool."""
    g = random_green(rng)
    grey = g.mean()
    g = lerp(g, np.array([grey, grey, grey]), 0.32)
    return g * np.array([0.95, 1.0, 1.05])


# ---------------------------------------------------------------------------------------------
# Pull-push flood fill -- identical machinery to tools/palm/make_frond.py. Builds a mip pyramid of
# alpha-weighted colour (a box downsample already computes the "pull" average for free) and walks
# back down handing any pixel with no colour of its own its parent's filled value, so every texel
# in the atlas ends up with a sensible colour in O(log2(size)) passes, however far it sits from any
# opaque leaf.
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


def _paint_polygon(color_acc, alpha_acc, poly_pts, color_fn, notches=None):
    """Rasterise one filled polygon into a small cropped buffer and splat it into the full
    accumulators, exactly as make_frond.py does. `notches` is an optional list of (cx, cy, r)
    circles subtracted from the mask afterwards -- insect damage on a leaf."""
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
    if notches:
        yy_grid, xx_grid = np.mgrid[0:y1 - y0, 0:x1 - x0]
        for ncx, ncy, nr in notches:
            lcx, lcy = ncx - x0, ncy - y0
            mask &= (xx_grid - lcx) ** 2 + (yy_grid - lcy) ** 2 > nr * nr
    if not mask.any():
        return
    yy, xx = np.nonzero(mask)
    gx = (xx + x0).astype(np.float64)
    gy = (yy + y0).astype(np.float64)
    rgb = color_fn(gx, gy)
    alpha_acc[yy + y0, xx + x0] = 255.0
    color_acc[yy + y0, xx + x0] = rgb


def _paint_mask(alpha_acc, poly_pts):
    """Rasterise one filled polygon's coverage into a colour-less alpha mask -- used to build the
    twig-only / leaf-only masks that the "no bare twig tip" check measures distance on."""
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
    mask = np.asarray(mask_img, dtype=np.float64)
    alpha_acc[y0:y1, x0:x1] = np.maximum(alpha_acc[y0:y1, x0:x1], mask)


def dilate_mask(mask, radius_px):
    """Binary dilation by `radius_px` -- a square structuring element (PIL's cheapest option),
    plenty precise for a "was there a leaf within N px" check."""
    if not mask.any():
        return mask
    size = 2 * int(round(radius_px)) + 1
    img = Image.fromarray((mask.astype(np.uint8) * 255), mode="L")
    img = img.filter(ImageFilter.MaxFilter(size))
    return np.asarray(img) > 127


def clamp_poly(poly, patch_ss, margin_ss):
    """Hard safety net for twigs/stems only: no vertex may sit inside the transparent margin,
    whatever the geometry above did -- guarantees the margin PASS/FAIL check below. Leaf blades
    are never clamped (see poly_fits) because clamping slices a blade flat and leaves a visible
    straight cut edge; a clamped stem is thin enough that this never shows."""
    lo = margin_ss + 1.0
    hi = patch_ss - margin_ss - 1.0
    return [(min(max(x, lo), hi), min(max(y, lo), hi)) for x, y in poly]


def poly_fits(poly, patch_ss, margin_ss):
    """True only if every vertex of a leaf blade already sits inside the safe zone. Leaves that
    fail are re-sampled (new position/angle/size) rather than clamped, so the clump's silhouette
    is built only from whole leaf tips and never shows a straight cut edge."""
    lo = margin_ss
    hi = patch_ss - margin_ss
    return all(lo <= x <= hi and lo <= y <= hi for x, y in poly)


def safe_zone_ring_fraction(sub_alpha, margin_px):
    """Fraction of the one-texel-wide ring just inside the safe zone (the innermost row/col of
    drawable pixels) that is opaque. A cut-out silhouette leaves a long straight run of opaque
    pixels hugging this ring; a silhouette built from whole leaf tips barely touches it."""
    n = sub_alpha.shape[0]
    lo, hi = margin_px, n - margin_px - 1
    if hi <= lo:
        return 0.0
    top = sub_alpha[lo, lo:hi + 1]
    bottom = sub_alpha[hi, lo:hi + 1]
    left = sub_alpha[lo:hi + 1, lo]
    right = sub_alpha[lo:hi + 1, hi]
    ring = np.concatenate([top, bottom, left, right])
    return float(np.mean(ring > 128)) if ring.size else 0.0


def leaf_underside(col, rng, frac=1.0 / 3.0):
    """About a third of leaves are shown from underneath: paler and greyer, the same hue lifted
    toward a dusty grey-green as if the leaf had turned over in the wind."""
    if rng.random() < frac:
        return lerp(col, np.array([150.0, 160.0, 120.0]), 0.40)
    return col


def _taper_quad(p0, p1, w0, w1, N):
    nx, ny = N
    return [
        (p0[0] + nx * w0 * 0.5, p0[1] + ny * w0 * 0.5),
        (p1[0] + nx * w1 * 0.5, p1[1] + ny * w1 * 0.5),
        (p1[0] - nx * w1 * 0.5, p1[1] - ny * w1 * 0.5),
        (p0[0] - nx * w0 * 0.5, p0[1] - ny * w0 * 0.5),
    ]


def _round_width(s, max_w, wave_amp, wave_freq, phase):
    """Sea-grape profile: narrow where it meets its twig, broad through the middle, a blunt
    round tip -- with a wavy-edge ripple layered on top."""
    base = smoothstep(0.0, 0.22, s) * (1.0 - smoothstep(0.82, 1.0, s)) ** 0.6
    w = max_w * base * (1.0 + wave_amp * np.sin(wave_freq * s * 2.0 * np.pi + phase))
    return np.clip(w, 0.0, None)


def _lance_width(s, max_w):
    """Bay-cedar profile: narrower, peaks earlier, tapers to a sharp point."""
    base = smoothstep(0.0, 0.10, s) * (1.0 - smoothstep(0.55, 1.0, s)) ** 1.4
    return np.clip(max_w * base, 0.0, None)


def _blade_polygon(root, D, N, length, max_w, kind, wave_amp, wave_freq, phase, rng, K=18):
    s_vals = np.linspace(0.0, 1.0, K)
    if kind == "round":
        w = _round_width(s_vals, max_w, wave_amp, wave_freq, phase)
    else:
        w = _lance_width(s_vals, max_w)
    curve = length * rng.uniform(-0.10, 0.10)  # a slight lateral bow, never a full taper-to-point
    ax, ay = root
    dx, dy = D
    nx, ny = N
    left, right = [], []
    for s, wi in zip(s_vals, w):
        cx = ax + dx * (s * length) + nx * curve * s * s
        cy = ay + dy * (s * length) + ny * curve * s * s
        left.append((cx + nx * wi * 0.5, cy + ny * wi * 0.5))
        right.append((cx - nx * wi * 0.5, cy - ny * wi * 0.5))
    return left + right[::-1]


def _make_veins(root, D, N, length, max_w, n_veins, wave_amp, wave_freq, phase, rng):
    """3-4 pale side veins branching off the midrib at a diagonal, each reaching most of the way
    to the leaf's own edge at that point along its length."""
    veins = []
    dx, dy = D
    nx, ny = N
    ax, ay = root
    positions = np.linspace(0.30, 0.85, n_veins)
    for k, vs in enumerate(positions):
        side = 1.0 if k % 2 == 0 else -1.0
        w_at = float(_round_width(np.array([vs]), max_w, wave_amp, wave_freq, phase)[0])
        vein_len = w_at * 0.5 * rng.uniform(0.75, 0.95)
        vang = math.radians(rng.uniform(28.0, 48.0))
        v_along, v_out = math.sin(vang), side * math.cos(vang)
        wx = dx * v_along + nx * v_out
        wy = dy * v_along + ny * v_out
        ox = ax + dx * vs * length
        oy = ay + dy * vs * length
        veins.append((ox, oy, wx, wy, vein_len))
    return veins


def make_color_fn(root, D, N, length, base_col, overall_mult, midrib, veins):
    ax, ay = root
    dx, dy = D
    nx, ny = N

    def color_fn(gx, gy):
        px, py = gx - ax, gy - ay
        s = np.clip((px * dx + py * dy) / max(length, 1e-6), 0.0, 1.0)
        perp = px * nx + py * ny
        col = np.tile(base_col, (gx.shape[0], 1)).astype(np.float64)
        ao = 1.0 - smoothstep(0.0, 0.12, s)  # fake AO: darker near the leaf's own root
        col = col * (1.0 - ao * 0.30)[:, None]
        col = col * overall_mult
        if midrib:
            near = np.abs(perp) < (SS * 0.55)
            col = np.where(near[:, None], col * 1.25, col)
        for ox, oy, wx, wy, vlen in veins:
            rx, ry = gx - ox, gy - oy
            along = rx * wx + ry * wy
            vperp = rx * (-wy) + ry * wx
            vmask = (along >= 0.0) & (along <= vlen) & (np.abs(vperp) < SS * 0.55)
            col = np.where(vmask[:, None], col * 1.25, col)
        return np.clip(col, 6.0, 255.0)

    return color_fn


def _fan_geometry(patch_ss, margin_ss):
    """Shared ellipse the leaf/twig tips aim for: rooted at the bottom centre of the patch,
    spanning ~85% of the patch's width and ~90% of its height inside the margin."""
    cx = patch_ss / 2.0
    pad = 0.02 * patch_ss
    root_y = patch_ss - margin_ss - pad
    ry = 0.45 * patch_ss
    rx = 0.425 * patch_ss
    cy = root_y - ry
    return cx, root_y, cx, cy, rx, ry


def _target_point(cx_ellipse, cy_ellipse, rx, ry, phi_rad):
    return cx_ellipse + rx * math.sin(phi_rad), cy_ellipse - ry * math.cos(phi_rad)


MAX_PLACE_TRIES = 20  # re-sample position/angle/size this many times before giving up on a leaf


def _place_filler_leaves(leaves, n_filler, patch_ss, margin_ss, cx, root_y, angle_span,
                          diam_mult, scale_px, kind, rng, target_cx, target_cy, spread_x, spread_y):
    """A batch of leaves that belong to no twig at all: scattered around (target_cx, target_cy)
    rather than along any twig, so that area of the clump reads as a dense mass instead of a
    hollow fan. Called twice per patch (see FILLER_FRACTION split below) -- once biased toward
    the clump's centre (for the centre-density check) and once biased toward the stem cluster at
    the patch's own bottom centre (so the twigs' converging root ends disappear too)."""
    for _ in range(n_filler):
        placed = None
        for _attempt in range(MAX_PLACE_TRIES):
            fx = target_cx + rng.normal(0.0, spread_x)
            fy = target_cy + rng.normal(0.0, spread_y)
            leaf_phi = rng.uniform(-angle_span, angle_span)
            phi_rad = math.radians(leaf_phi)
            D = (math.sin(phi_rad), -math.cos(phi_rad))
            N = (math.cos(phi_rad), math.sin(phi_rad))
            if kind == "lance":
                blade_length = rng.uniform(50.0, 86.5) * scale_px
                max_w = blade_length / rng.uniform(2.6, 3.2)
            else:
                max_w = rng.uniform(34.6, 65.4) * diam_mult * scale_px
                blade_length = max_w * rng.uniform(0.95, 1.25)
            wave_amp = rng.uniform(0.05, 0.12) if kind == "round" else 0.0
            wave_freq = rng.uniform(2.0, 3.2) if kind == "round" else 0.0
            phase = rng.uniform(0.0, 2.0 * math.pi) if kind == "round" else 0.0
            attach = (fx, fy)
            poly = _blade_polygon(attach, D, N, blade_length, max_w,
                                   "round" if kind == "round" else "lance",
                                   wave_amp, wave_freq, phase, rng)
            if poly_fits(poly, patch_ss, margin_ss):
                placed = dict(attach=attach, D=D, N=N, blade_length=blade_length, max_w=max_w,
                              reach=math.hypot(fx - cx, fy - root_y) + blade_length, phi=leaf_phi,
                              poly=poly, wave_amp=wave_amp, wave_freq=wave_freq, phase=phase,
                              filler=True)
                break
        if placed is not None:
            leaves.append(placed)


def build_round_patch(kind, patch_ss, margin_ss, scale_px, rng):
    """Sea-grape family: `kind` is "seagrape" (patches 0, 3) or "sparse" (patch 2)."""
    color_acc = np.zeros((patch_ss, patch_ss, 3), dtype=np.float64)
    alpha_acc = np.zeros((patch_ss, patch_ss), dtype=np.float64)
    twig_alpha_acc = np.zeros((patch_ss, patch_ss), dtype=np.float64)
    leaf_alpha_acc = np.zeros((patch_ss, patch_ss), dtype=np.float64)

    cx, root_y, cx_e, cy_e, rx, ry = _fan_geometry(patch_ss, margin_ss)

    if kind == "seagrape":
        n_twigs = int(rng.integers(10, 15))
        angle_span = 95.0
        diam_mult = 1.0
    else:
        n_twigs = int(rng.integers(10, 14))
        angle_span = 100.0
        diam_mult = 1.25

    idx = np.arange(n_twigs)
    even = (idx + 0.5) / n_twigs
    phi0 = -angle_span + 2.0 * angle_span * even  # base slot angle; jitter is re-rolled per twig

    # Twig skeletons first, drawn hair-thin and dark (TWIG_W_MIN/MAX, TWIG_COLOR) so they read as
    # shadow between leaves rather than as branches.
    twigs = []
    twig_widths_ss = []
    for i in range(n_twigs):
        phi_deg = float(np.clip(phi0[i] + rng.normal(0.0, 4.0), -108.0, 108.0))
        phi_rad = math.radians(phi_deg)
        D = (math.sin(phi_rad), -math.cos(phi_rad))
        N = (math.cos(phi_rad), math.sin(phi_rad))
        tx, ty = _target_point(cx_e, cy_e, rx, ry, phi_rad)
        root = (cx + rng.uniform(-0.02, 0.02) * patch_ss, root_y + rng.uniform(-0.01, 0.01) * patch_ss)
        total_reach = math.hypot(tx - root[0], ty - root[1])
        twig_len = max(8.0 * scale_px, total_reach * rng.uniform(0.55, 0.95))
        tip = (root[0] + D[0] * twig_len, root[1] + D[1] * twig_len)
        twigs.append(dict(root=root, tip=tip, D=D, N=N, phi=phi_deg, twig_len=twig_len))

        tw0 = rng.uniform(TWIG_W_MIN, TWIG_W_MAX) * scale_px
        tw1 = tw0 * 0.45
        twig_widths_ss.append(tw0)
        twig_poly = clamp_poly(_taper_quad(root, tip, tw0, tw1, N), patch_ss, margin_ss)
        twig_col = TWIG_COLOR * rng.uniform(0.90, 1.10)
        _paint_polygon(color_acc, alpha_acc, twig_poly,
                       lambda gx, gy, c=twig_col: np.tile(c, (gx.shape[0], 1)))
        _paint_mask(twig_alpha_acc, twig_poly)

    # Each leaf is sampled up to MAX_PLACE_TRIES times: if its blade would cross the safe-zone
    # boundary it is rejected and re-placed with a fresh angle/size rather than clamped, so the
    # clump's outline is made only of whole leaf tips. A leaf that never fits is skipped. Every
    # twig carries 3-6 of these, spread along its outer 65% at varied distances, with one always
    # at or past the twig's own tip (frac >= 1) so no bare stick end ever shows.
    leaves = []
    for t in twigs:
        n_leaf = int(rng.integers(4, 7))
        fracs = list(rng.uniform(0.35, 0.94, max(n_leaf - 1, 1)))
        fracs.append(rng.uniform(0.98, 1.12))
        for frac in fracs:
            placed = None
            for _attempt in range(MAX_PLACE_TRIES):
                leaf_phi = t["phi"] + rng.uniform(-10.0, 10.0)
                phi_rad = math.radians(leaf_phi)
                D = (math.sin(phi_rad), -math.cos(phi_rad))
                N = (math.cos(phi_rad), math.sin(phi_rad))
                attach = (t["root"][0] + t["D"][0] * frac * t["twig_len"],
                          t["root"][1] + t["D"][1] * frac * t["twig_len"])
                diameter_px = rng.uniform(34.6, 65.4) * diam_mult * scale_px
                blade_length = diameter_px * rng.uniform(0.95, 1.25)
                wave_amp = rng.uniform(0.05, 0.12)
                wave_freq = rng.uniform(2.0, 3.2)
                phase = rng.uniform(0.0, 2.0 * math.pi)
                poly = _blade_polygon(attach, D, N, blade_length, diameter_px, "round",
                                        wave_amp, wave_freq, phase, rng)
                if poly_fits(poly, patch_ss, margin_ss):
                    placed = dict(attach=attach, D=D, N=N, blade_length=blade_length, max_w=diameter_px,
                                  reach=frac * t["twig_len"] + blade_length, phi=leaf_phi, poly=poly,
                                  wave_amp=wave_amp, wave_freq=wave_freq, phase=phase, filler=False)
                    break
            if placed is not None:
                leaves.append(placed)

    # About FILLER_FRACTION of the total belong to no twig: most scattered near the clump's
    # centre so the middle reads as a dense mass, the rest near the stem cluster itself so the
    # twigs' converging root ends disappear into the same mass.
    n_filler = int(round(len(leaves) * (FILLER_FRACTION / (1.0 - FILLER_FRACTION))))
    n_base = int(round(n_filler * 0.22))
    _place_filler_leaves(leaves, n_filler - n_base, patch_ss, margin_ss, cx, root_y, angle_span,
                         diam_mult, scale_px, "round", rng,
                         patch_ss * 0.5, patch_ss * 0.45, 0.12 * patch_ss, 0.12 * patch_ss)
    _place_filler_leaves(leaves, n_base, patch_ss, margin_ss, cx, root_y, angle_span,
                         diam_mult, scale_px, "round", rng,
                         cx, root_y - 0.04 * patch_ss, 0.14 * patch_ss, 0.07 * patch_ss)

    # Bronze is capped at a quarter of all leaves (twig-borne and filler alike).
    bronze_cap = len(leaves) // 4
    if bronze_cap > 0:
        for k in rng.choice(len(leaves), size=bronze_cap, replace=False):
            leaves[k]["bronze"] = True

    max_reach = max((l["reach"] for l in leaves), default=1.0)
    blade_widths = [l["max_w"] for l in leaves]
    depth = rng.uniform(0.0, 1.0, len(leaves))
    for i in np.argsort(depth):
        L = leaves[int(i)]
        has_damage = kind == "sparse" and rng.random() < 0.35

        base_col = BRONZE * rng.uniform(0.90, 1.10) if L.get("bronze") else random_green(rng)
        base_col = leaf_underside(base_col, rng)
        orientation = 0.5 + 0.5 * math.cos(math.radians(L["phi"] - LIGHT_DIR_DEG))
        brightness_mult = 0.62 + 0.64 * orientation
        ao_mult = 0.80 + 0.20 * np.clip(L["reach"] / max_reach, 0.0, 1.0)
        overall_mult = brightness_mult * ao_mult

        wave_amp, wave_freq, phase = L["wave_amp"], L["wave_freq"], L["phase"]
        poly = L["poly"]
        n_veins = int(rng.integers(3, 5))
        veins = _make_veins(L["attach"], L["D"], L["N"], L["blade_length"], L["max_w"], n_veins,
                             wave_amp, wave_freq, phase, rng)
        color_fn = make_color_fn(L["attach"], L["D"], L["N"], L["blade_length"], base_col,
                                  overall_mult, True, veins)

        notches = []
        if has_damage:
            for _ in range(int(rng.integers(1, 3))):
                s = rng.uniform(0.40, 0.90)
                w_at = float(_round_width(np.array([s]), L["max_w"], wave_amp, wave_freq, phase)[0])
                side = rng.choice([-1.0, 1.0])
                s_len = s * L["blade_length"]
                nx_ = L["attach"][0] + L["D"][0] * s_len + L["N"][0] * side * w_at * 0.5 * rng.uniform(0.6, 0.95)
                ny_ = L["attach"][1] + L["D"][1] * s_len + L["N"][1] * side * w_at * 0.5 * rng.uniform(0.6, 0.95)
                notches.append((nx_, ny_, rng.uniform(0.12, 0.22) * w_at))
        _paint_polygon(color_acc, alpha_acc, poly, color_fn, notches=notches)
        _paint_mask(leaf_alpha_acc, poly)

    return color_acc, alpha_acc, blade_widths, twig_widths_ss, twig_alpha_acc, leaf_alpha_acc


def build_baycedar_patch(patch_ss, margin_ss, scale_px, rng):
    color_acc = np.zeros((patch_ss, patch_ss, 3), dtype=np.float64)
    alpha_acc = np.zeros((patch_ss, patch_ss), dtype=np.float64)
    twig_alpha_acc = np.zeros((patch_ss, patch_ss), dtype=np.float64)
    leaf_alpha_acc = np.zeros((patch_ss, patch_ss), dtype=np.float64)

    cx, root_y, cx_e, cy_e, rx, ry = _fan_geometry(patch_ss, margin_ss)

    n_twigs = int(rng.integers(24, 32))
    angle_span = 92.0
    idx = np.arange(n_twigs)
    even = (idx + 0.5) / n_twigs
    phi = -angle_span + 2.0 * angle_span * even
    phi = phi + rng.normal(0.0, 5.0, n_twigs)

    twigs = []
    twig_widths_ss = []
    for i in range(n_twigs):
        phi_deg = float(phi[i])
        phi_rad = math.radians(phi_deg)
        D = (math.sin(phi_rad), -math.cos(phi_rad))
        N = (math.cos(phi_rad), math.sin(phi_rad))
        tx, ty = _target_point(cx_e, cy_e, rx, ry, phi_rad)
        root = (cx + rng.uniform(-0.02, 0.02) * patch_ss, root_y + rng.uniform(-0.01, 0.01) * patch_ss)
        total_reach = math.hypot(tx - root[0], ty - root[1])
        twig_len = total_reach * rng.uniform(0.55, 0.85)
        tip = (root[0] + D[0] * twig_len, root[1] + D[1] * twig_len)
        twigs.append(dict(root=root, tip=tip, D=D, N=N, phi=phi_deg, twig_len=twig_len))

        tw0 = rng.uniform(TWIG_W_MIN, TWIG_W_MAX) * scale_px
        tw1 = tw0 * 0.45
        twig_widths_ss.append(tw0)
        twig_poly = clamp_poly(_taper_quad(root, tip, tw0, tw1, N), patch_ss, margin_ss)
        twig_col = TWIG_COLOR * rng.uniform(0.90, 1.10)
        _paint_polygon(color_acc, alpha_acc, twig_poly,
                       lambda gx, gy, c=twig_col: np.tile(c, (gx.shape[0], 1)))
        _paint_mask(twig_alpha_acc, twig_poly)

    # Each leaflet is sampled up to MAX_PLACE_TRIES times (fresh angle + size per try): one whose
    # blade would cross the safe-zone boundary is rejected and re-placed rather than clamped, so
    # the whorl's outline is made only of whole leaflet tips. A leaflet that never fits is skipped.
    # Every twig carries 3-6 of these, spread along its outer 65% at varied distances, with one
    # always at or past the twig's own tip so no bare stick end ever shows.
    leaflets = []
    for t in twigs:
        n_leaf = int(rng.integers(4, 7))
        fracs = list(rng.uniform(0.35, 0.94, max(n_leaf - 1, 1)))
        fracs.append(rng.uniform(0.98, 1.12))
        for frac in fracs:
            placed = None
            for _attempt in range(MAX_PLACE_TRIES):
                leaf_phi = t["phi"] + rng.uniform(-22.0, 22.0)
                phi_rad = math.radians(leaf_phi)
                D = (math.sin(phi_rad), -math.cos(phi_rad))
                N = (math.cos(phi_rad), math.sin(phi_rad))
                node = (t["root"][0] + t["D"][0] * frac * t["twig_len"],
                        t["root"][1] + t["D"][1] * frac * t["twig_len"])
                blade_length = rng.uniform(50.0, 86.5) * scale_px
                max_w = blade_length / rng.uniform(2.6, 3.2)
                poly_lf = _blade_polygon(node, D, N, blade_length, max_w, "lance", 0.0, 0.0, 0.0, rng)
                if poly_fits(poly_lf, patch_ss, margin_ss):
                    placed = dict(node=node, phi=leaf_phi, reach=frac * t["twig_len"] + blade_length,
                                  D=D, N=N, blade_length=blade_length, max_w=max_w, poly=poly_lf)
                    break
            if placed is not None:
                leaflets.append(placed)

    # About FILLER_FRACTION of the total belong to no twig: most scattered near the clump's
    # centre so the middle of the whorl reads as a dense mass, the rest near the stem cluster
    # itself so the twigs' converging root ends disappear into the same mass.
    n_filler = int(round(len(leaflets) * (FILLER_FRACTION / (1.0 - FILLER_FRACTION))))
    n_base = int(round(n_filler * 0.22))
    _place_filler_leaves(leaflets, n_filler - n_base, patch_ss, margin_ss, cx, root_y, angle_span,
                         1.0, scale_px, "lance", rng,
                         patch_ss * 0.5, patch_ss * 0.45, 0.12 * patch_ss, 0.12 * patch_ss)
    _place_filler_leaves(leaflets, n_base, patch_ss, margin_ss, cx, root_y, angle_span,
                         1.0, scale_px, "lance", rng,
                         cx, root_y - 0.04 * patch_ss, 0.14 * patch_ss, 0.07 * patch_ss)
    for lf in leaflets:
        lf.setdefault("node", lf.get("attach"))

    max_reach = max((lf["reach"] for lf in leaflets), default=1.0)
    blade_widths = [lf["max_w"] for lf in leaflets]
    depth = rng.uniform(0.0, 1.0, len(leaflets))
    for i in np.argsort(depth):
        lf = leaflets[int(i)]
        base_col = bay_cedar_green(rng)
        base_col = leaf_underside(base_col, rng)
        orientation = 0.5 + 0.5 * math.cos(math.radians(lf["phi"] - LIGHT_DIR_DEG))
        brightness_mult = 0.62 + 0.64 * orientation
        ao_mult = 0.80 + 0.20 * np.clip(lf["reach"] / max_reach, 0.0, 1.0)
        overall_mult = brightness_mult * ao_mult
        color_fn = make_color_fn(lf["node"], lf["D"], lf["N"], lf["blade_length"], base_col,
                                  overall_mult, True, [])
        _paint_polygon(color_acc, alpha_acc, lf["poly"], color_fn)
        _paint_mask(leaf_alpha_acc, lf["poly"])

    return color_acc, alpha_acc, blade_widths, twig_widths_ss, twig_alpha_acc, leaf_alpha_acc


PATCH_KINDS = [("seagrape", 0, 0), ("baycedar", 0, 1), ("sparse", 1, 0), ("seagrape", 1, 1)]


def make_leafpatch(size, seed):
    patch_size = size // 2
    # scale_px converts a size given in FINAL-resolution pixels (as the spec's leaf/stem
    # dimensions are) into this patch's supersampled drawing space: once for the atlas's own
    # size relative to the reference 512px patch, and once for the SS supersample factor itself.
    scale_px = (patch_size / 512.0) * SS
    margin_ss = MARGIN_PX * SS
    patch_ss = patch_size * SS
    Wss = size * SS

    color_full = np.zeros((Wss, Wss, 3), dtype=np.float64)
    alpha_full = np.zeros((Wss, Wss), dtype=np.float64)
    twig_alpha_full = np.zeros((Wss, Wss), dtype=np.float64)
    leaf_alpha_full = np.zeros((Wss, Wss), dtype=np.float64)

    rng = np.random.default_rng(seed)
    patch_blade_widths_ss = []
    patch_twig_widths_ss = []
    for kind, row, col in PATCH_KINDS:
        if kind == "baycedar":
            c_acc, a_acc, widths, twig_widths, t_acc, l_acc = build_baycedar_patch(
                patch_ss, margin_ss, scale_px, rng)
        else:
            c_acc, a_acc, widths, twig_widths, t_acc, l_acc = build_round_patch(
                kind, patch_ss, margin_ss, scale_px, rng)
        y0, x0 = row * patch_ss, col * patch_ss
        color_full[y0:y0 + patch_ss, x0:x0 + patch_ss] = c_acc
        alpha_full[y0:y0 + patch_ss, x0:x0 + patch_ss] = a_acc
        twig_alpha_full[y0:y0 + patch_ss, x0:x0 + patch_ss] = t_acc
        leaf_alpha_full[y0:y0 + patch_ss, x0:x0 + patch_ss] = l_acc
        patch_blade_widths_ss.append(widths)
        patch_twig_widths_ss.append(twig_widths)

    # Premultiplied box downsample -- same reasoning as make_frond.py: averaging colour and alpha
    # independently would smear black into every leaf edge.
    premult = color_full * (alpha_full / 255.0)[:, :, None]
    premult_d = premult.reshape(size, SS, size, SS, 3).mean(axis=(1, 3))
    alpha_d = alpha_full.reshape(size, SS, size, SS).mean(axis=(1, 3))
    with np.errstate(invalid="ignore", divide="ignore"):
        rgb_d = np.where(alpha_d[:, :, None] > 1e-6, premult_d / np.maximum(alpha_d[:, :, None] / 255.0, 1e-9), 0.0)

    # Twig-only and leaf-only coverage masks, downsampled the plain (non-premultiplied) way since
    # they carry no colour of their own -- just "was a twig/leaf polygon painted here".
    twig_alpha_d = twig_alpha_full.reshape(size, SS, size, SS).mean(axis=(1, 3))
    leaf_alpha_d = leaf_alpha_full.reshape(size, SS, size, SS).mean(axis=(1, 3))

    # Per-patch coverage, twig-tip, centre-density and margin checks at final resolution.
    patch_reports = []
    for i, (kind, row, col) in enumerate(PATCH_KINDS):
        y0, x0 = row * patch_size, col * patch_size
        sub_alpha = alpha_d[y0:y0 + patch_size, x0:x0 + patch_size]
        coverage = float(np.mean(sub_alpha) / 255.0)

        edges = {
            "top": sub_alpha[0:MARGIN_PX, :],
            "bottom": sub_alpha[patch_size - MARGIN_PX:patch_size, :],
            "left": sub_alpha[:, 0:MARGIN_PX],
            "right": sub_alpha[:, patch_size - MARGIN_PX:patch_size],
        }
        edge_results = {}
        for name, strip in edges.items():
            peak = float(strip.max()) if strip.size else 0.0
            edge_results[name] = (peak <= 0.5, peak)

        ring_frac = safe_zone_ring_fraction(sub_alpha, MARGIN_PX)

        widths_final = np.asarray(patch_blade_widths_ss[i], dtype=np.float64) / SS
        mean_width = float(widths_final.mean()) if widths_final.size else 0.0
        max_width = float(widths_final.max()) if widths_final.size else 0.0

        twig_widths_final = np.asarray(patch_twig_widths_ss[i], dtype=np.float64) / SS
        twig_mean_width = float(twig_widths_final.mean()) if twig_widths_final.size else 0.0
        twig_max_width = float(twig_widths_final.max()) if twig_widths_final.size else 0.0

        # "No twig may stick out past its leaves": a twig texel that still reads as its own
        # colour (no leaf drawn over it) with no leaf texel within DILATE_PX counts as a bare
        # stick end. Fraction is of the patch's total opaque texel count.
        sub_twig = twig_alpha_d[y0:y0 + patch_size, x0:x0 + patch_size]
        sub_leaf = leaf_alpha_d[y0:y0 + patch_size, x0:x0 + patch_size]
        twig_opaque = sub_twig > 127
        leaf_opaque = sub_leaf > 127
        overall_opaque = sub_alpha > 127
        twig_visible = twig_opaque & ~leaf_opaque
        leaf_dilated = dilate_mask(leaf_opaque, DILATE_PX)
        bare_twig = twig_visible & ~leaf_dilated
        bare_twig_frac = float(bare_twig.sum()) / float(max(int(overall_opaque.sum()), 1))

        # "The middle of the clump must be dense": opaque fraction inside a central box a third
        # of the patch wide/tall, centred at 50% across and 55% up the patch (from the bottom).
        box_w = patch_size // 3
        box_h = patch_size // 3
        box_cx = int(round(patch_size * 0.5))
        box_cy = int(round(patch_size * (1.0 - 0.55)))
        bx0, bx1 = max(0, box_cx - box_w // 2), min(patch_size, box_cx + box_w // 2)
        by0, by1 = max(0, box_cy - box_h // 2), min(patch_size, box_cy + box_h // 2)
        box = sub_alpha[by0:by1, bx0:bx1]
        box_density = float(np.mean(box > 128)) if box.size else 0.0

        patch_reports.append(dict(index=i, kind=kind, coverage=coverage, edges=edge_results,
                                   ring_frac=ring_frac, mean_width=mean_width, max_width=max_width,
                                   twig_mean_width=twig_mean_width, twig_max_width=twig_max_width,
                                   bare_twig_frac=bare_twig_frac, box_density=box_density))

    # Flood colour outward into every fully-transparent texel of the whole atlas -- identical to
    # make_frond.py, including the deep-background blend toward the global alpha-weighted mean so
    # texels far from any leaf don't just inherit whatever's locally nearest.
    filled_rgb = pull_push(rgb_d, alpha_d)
    global_mean = np.average(rgb_d.reshape(-1, 3), axis=0, weights=alpha_d.reshape(-1) + 1e-9)
    deep_bg = 1.0 - smoothstep(0.0, 40.0, alpha_d)
    filled_rgb = filled_rgb * (1.0 - deep_bg[:, :, None] * 0.85) + global_mean[None, None, :] * (
        deep_bg[:, :, None] * 0.85
    )
    filled_rgb = np.clip(filled_rgb, 6.0, 255.0)

    out = np.zeros((size, size, 4), dtype=np.uint8)
    out[:, :, :3] = np.clip(np.round(filled_rgb), 0, 255).astype(np.uint8)
    out[:, :, 3] = np.clip(np.round(alpha_d), 0, 255).astype(np.uint8)

    low_mask = out[:, :, 3] < 8
    high_mask = out[:, :, 3] > 200
    low_mean = out[:, :, :3][low_mask].astype(np.float64).mean(axis=0) if low_mask.any() else np.zeros(3)
    high_mean = out[:, :, :3][high_mask].astype(np.float64).mean(axis=0) if high_mask.any() else np.zeros(3)

    stats = dict(patch_reports=patch_reports, low_mean=low_mean, high_mean=high_mean,
                 patch_size=patch_size)
    return out, stats


def make_preview(rgba, path, patch_size):
    h, w = rgba.shape[:2]
    tile = 16
    yy, xx = np.meshgrid(np.arange(h) // tile, np.arange(w) // tile, indexing="ij")
    checker = np.where((xx + yy) % 2 == 0, 96, 150).astype(np.uint8)
    bg = np.repeat(checker[:, :, None], 3, axis=2).astype(np.float64)

    fg = rgba[:, :, :3].astype(np.float64)
    a = (rgba[:, :, 3:4].astype(np.float64)) / 255.0
    composited = fg * a + bg * (1.0 - a)
    composited = np.clip(np.round(composited), 0, 255).astype(np.uint8)

    comp_img = Image.fromarray(composited, mode="RGB")
    draw = ImageDraw.Draw(comp_img)
    draw.line([(patch_size, 0), (patch_size, h)], fill=(255, 40, 40), width=1)
    draw.line([(0, patch_size), (w, patch_size)], fill=(255, 40, 40), width=1)

    alpha_rgb = np.repeat(rgba[:, :, 3:4], 3, axis=2)
    alpha_img = Image.fromarray(alpha_rgb, mode="RGB")

    gap = Image.new("RGB", (8, h), (255, 255, 255))
    combo = Image.new("RGB", (w * 2 + 8, h))
    combo.paste(comp_img, (0, 0))
    combo.paste(gap, (w, 0))
    combo.paste(alpha_img, (w + 8, 0))
    combo.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--size", type=int, default=1024, help="whole atlas is size x size (2x2 patches)")
    ap.add_argument("--preview", default=None)
    ap.add_argument("--out", default=DEFAULT_OUT)
    args = ap.parse_args()

    if args.size % 2 != 0:
        raise SystemExit("--size must be even (it is split into a 2x2 grid of equal patches)")

    rgba, stats = make_leafpatch(args.size, args.seed)
    patch_size = stats["patch_size"]

    print(f"atlas size {args.size}x{args.size}, 4 patches of {patch_size}x{patch_size}, margin {MARGIN_PX}px")
    for rep in stats["patch_reports"]:
        print(f"patch {rep['index']} ({rep['kind']}): coverage {rep['coverage']:.4f} (target 0.34..0.50)")

    print("blade width at final resolution (target ~55..90 px):")
    for rep in stats["patch_reports"]:
        print(f"  patch {rep['index']} ({rep['kind']}): mean {rep['mean_width']:.1f}px, "
              f"max {rep['max_width']:.1f}px")

    print("twig width at final resolution (target <= 2px):")
    for rep in stats["patch_reports"]:
        print(f"  patch {rep['index']} ({rep['kind']}): mean {rep['twig_mean_width']:.2f}px, "
              f"max {rep['twig_max_width']:.2f}px")

    ring_all_pass = True
    print("safe-zone ring checks (PASS/FAIL, fraction of the ring just inside the safe zone that "
          "is opaque, must be < 0.04):")
    for rep in stats["patch_reports"]:
        ok = rep["ring_frac"] < 0.04
        ring_all_pass = ring_all_pass and ok
        print(f"  patch {rep['index']} ({rep['kind']}): {'PASS' if ok else 'FAIL'} "
              f"(ring opaque fraction {rep['ring_frac']:.4f})")
    print(f"all safe-zone ring checks: {'PASS' if ring_all_pass else 'FAIL'}")

    bare_all_pass = True
    print("no-bare-twig-tip checks (PASS/FAIL, fraction of opaque texels that are twig-coloured "
          "with no leaf texel within 6px, must be < 0.02):")
    for rep in stats["patch_reports"]:
        ok = rep["bare_twig_frac"] < 0.02
        bare_all_pass = bare_all_pass and ok
        print(f"  patch {rep['index']} ({rep['kind']}): {'PASS' if ok else 'FAIL'} "
              f"(bare-twig fraction {rep['bare_twig_frac']:.4f})")
    print(f"all no-bare-twig-tip checks: {'PASS' if bare_all_pass else 'FAIL'}")

    density_all_pass = True
    print("centre-density checks (PASS/FAIL, opaque fraction of the central third-by-third box, "
          "must be >= 0.62):")
    for rep in stats["patch_reports"]:
        ok = rep["box_density"] >= 0.62
        density_all_pass = density_all_pass and ok
        print(f"  patch {rep['index']} ({rep['kind']}): {'PASS' if ok else 'FAIL'} "
              f"(central box opaque fraction {rep['box_density']:.4f})")
    print(f"all centre-density checks: {'PASS' if density_all_pass else 'FAIL'}")

    seam_edges = {
        # the two edges of each patch that border ANOTHER patch -- the eight that matter most for
        # "no opaque texel may cross into a neighbour" via bilinear filtering at the seam.
        0: ("right", "bottom"),
        1: ("left", "bottom"),
        2: ("right", "top"),
        3: ("left", "top"),
    }
    all_pass = True
    print("margin checks (PASS/FAIL, peak alpha found in an 8px+ margin strip that must be 0):")
    for rep in stats["patch_reports"]:
        i = rep["index"]
        for name in ("top", "bottom", "left", "right"):
            ok, peak = rep["edges"][name]
            all_pass = all_pass and ok
            tag = " [seam]" if name in seam_edges[i] else ""
            print(f"  patch {i} {name:6s}{tag}: {'PASS' if ok else 'FAIL'} (peak alpha {peak:.2f})")
    print(f"all margin checks: {'PASS' if all_pass else 'FAIL'}")

    low, high = stats["low_mean"], stats["high_mean"]
    print(f"mean RGB alpha<8:   {low[0]:.1f},{low[1]:.1f},{low[2]:.1f}")
    print(f"mean RGB alpha>200: {high[0]:.1f},{high[1]:.1f},{high[2]:.1f}")
    denom = np.maximum(high, 1e-6)
    rel_diff = np.max(np.abs(low - high) / denom)
    print(f"max relative channel difference: {rel_diff * 100:.1f}% (must be within 25%)")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    img = Image.fromarray(rgba, mode="RGBA")
    img.save(args.out)
    print(f"wrote {args.out} ({args.size}x{args.size}, {os.path.getsize(args.out) / 1024.0:.1f} kB)")

    if args.preview:
        os.makedirs(os.path.dirname(args.preview), exist_ok=True)
        make_preview(rgba, args.preview, patch_size)
        print(f"wrote preview {args.preview}")


if __name__ == "__main__":
    main()

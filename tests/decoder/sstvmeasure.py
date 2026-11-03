#!/usr/bin/env python3
"""Compare a decoded SSTV image with the image that was sent.

  sstvmeasure.py decoded.png reference.png

For every row the horizontal shift against the reference is found (smallest squared error).
  slant   shift of the last row minus the first row (pixels, robust linear fit)
  offset  shift at the first row (pixels)
  good    percentage of the rows that are in the right place: they resemble the sent row and
          their shift is within 2 pixels of the fitted line (noise doesn't count, misplaced rows do)
  rmse    remaining difference of the good rows (0..255), i.e. the noise in the image
"""
import sys
import numpy as np
from PIL import Image

MAXSHIFT = 40


def row_errors(dec, ref, shifts):
    w = dec.shape[0]
    core = slice(MAXSHIFT, w - MAXSHIFT)
    return np.array([np.mean((np.roll(dec, -s, axis=0)[core] - ref[core]) ** 2) for s in shifts])


def quality(err, ref):
    """1 - error/variance of the reference row, 1 is a perfect match"""
    var = np.var(ref[MAXSHIFT:-MAXSHIFT])
    return 1 - err / var if var > 100 else 0.0


def row_shift(dec, ref):
    """best shift of dec against ref, its quality and whether it is unambiguous
    (periodic content, like the vertical lines of the test pattern, matches at several shifts)"""
    shifts = np.arange(-MAXSHIFT, MAXSHIFT + 1)
    e = row_errors(dec, ref, shifts)
    i = int(np.argmin(e))
    others = e[np.abs(shifts - shifts[i]) >= 5]
    unique = others.min() > 1.5 * e[i] + 1
    return int(shifts[i]), quality(e[i], ref), unique


def measure(dec_fn, ref_fn):
    ref = np.asarray(Image.open(ref_fn).convert('RGB'), dtype=float)
    dec = np.asarray(Image.open(dec_fn).convert('RGB').resize((ref.shape[1], ref.shape[0])), dtype=float)
    h = ref.shape[0]
    res = dict(good=0.0, slant=float('nan'), offset=float('nan'), rmse=float('nan'))
    # rows without detail (one colour) can not be measured, they don't count
    measurable = np.array([np.var(ref[y][MAXSHIFT:-MAXSHIFT]) > 100 for y in range(h)])
    # 1: the line through the unambiguous rows (robust fit)
    first = [row_shift(dec[y], ref[y]) for y in range(h)]
    rows = np.array([y for y in range(h) if measurable[y] and first[y][1] > 0.3 and first[y][2]])
    if len(rows) < 10:
        return res
    sh = np.array([first[y][0] for y in rows], dtype=float)
    k, m = np.polyfit(rows, sh, 1)
    for _ in range(5):
        keep = np.abs(sh - (k * rows + m)) <= 2
        if keep.sum() < 10:
            break
        k, m = np.polyfit(rows[keep], sh[keep], 1)
    # 2: every row is searched close to the line
    good = np.zeros(h, bool)
    err = []
    for y in range(h):
        if not measurable[y]:
            continue
        c = int(round(k * y + m))
        cand = np.arange(c - 8, c + 9)
        cand = cand[np.abs(cand) <= MAXSHIFT]
        if len(cand) == 0:
            continue
        e = row_errors(dec[y], ref[y], cand)
        i = int(np.argmin(e))
        if quality(e[i], ref[y]) > 0.3 and abs(cand[i] - (k * y + m)) <= 2:
            good[y] = True
            err.append(e[i])
    res['good'] = 100.0 * good.sum() / max(1, measurable.sum())
    res['slant'] = k * (h - 1)
    res['offset'] = m
    if err:
        res['rmse'] = float(np.sqrt(np.mean(err)))
    return res


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    r = measure(sys.argv[1], sys.argv[2])
    print(f"good {r['good']:5.1f}%  slant {r['slant']:+6.1f} px  offset {r['offset']:+6.1f} px  rmse {r['rmse']:5.1f}")


if __name__ == '__main__':
    main()

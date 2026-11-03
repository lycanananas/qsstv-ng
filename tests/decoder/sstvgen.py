#!/usr/bin/env python3
"""Generate a reference SSTV signal (exact standard timing) as a 16 bit 48 kHz WAV.

  sstvgen.py MODE out.wav [--image file] [--ppm N] [--lead S] [--tail S]

MODE: M1, S1, PD120, R36
--ppm   clock error of the sender in ppm (positive: sender clock fast, lines shorter)
--image picture to send (default: built-in test pattern), it is scaled to the mode size
A PNG of the image as sent is written next to the WAV (<out>.ref.png), used by sstvmeasure.py.
"""
import argparse
import numpy as np
from PIL import Image

FS = 48000

# VIS code including the parity bit (bit 7), as in QSSTV's mode table
MODES = {
    'M1':    dict(vis=0xAC, w=320, h=256),
    'S1':    dict(vis=0x3C, w=320, h=256),
    'PD120': dict(vis=0x5F, w=640, h=496),
    'R36':   dict(vis=0x88, w=320, h=240),
}


def freq(v):
    """pixel value 0..255 -> frequency"""
    return 1500.0 + np.asarray(v, dtype=float) * 800.0 / 255.0


def test_pattern(w, h):
    """colour bars, grey ramp and vertical/diagonal lines, which show slant and offset"""
    img = np.full((h, w, 3), 255, np.uint8)
    bars = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
            (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
    bh = h // 4
    for i, c in enumerate(bars):
        img[:bh, i * w // 8:(i + 1) * w // 8] = c
    ramp = np.linspace(0, 255, w).astype(np.uint8)
    img[bh:2 * bh] = ramp[None, :, None]
    lines = img[2 * bh:]
    lines[:] = 255
    for x in range(0, w, w // 16):
        lines[:, x:x + max(2, w // 160)] = 0
    for y in range(lines.shape[0]):
        x = (y * 2) % w
        lines[y, x:x + 3] = (255, 0, 0)
    return img


def ycc(img):
    r, g, b = [img[..., i].astype(float) for i in range(3)]
    y = 16 + (65.738 * r + 129.057 * g + 25.064 * b) / 256
    v = 128 + (112.439 * r - 94.154 * g - 18.285 * b) / 256  # R-Y
    u = 128 + (-37.945 * r - 74.494 * g + 112.439 * b) / 256  # B-Y
    return [np.clip(c, 0, 255) for c in (y, u, v)]


class Seq:
    """list of (frequency, duration) segments"""

    def __init__(self):
        self.f = []
        self.d = []

    def tone(self, f, t):
        self.f.append(f)
        self.d.append(t)

    def scan(self, values, t):
        values = np.asarray(values)
        for v in freq(values):
            self.tone(v, t / len(values))


def vis(seq, code):
    seq.tone(1900, 0.300)
    seq.tone(1200, 0.010)
    seq.tone(1900, 0.300)
    seq.tone(1200, 0.030)
    for i in range(8):
        seq.tone(1100 if (code >> i) & 1 else 1300, 0.030)
    seq.tone(1200, 0.030)


def martin1(seq, img):
    for row in img:
        seq.tone(1200, 0.004862)
        seq.tone(1500, 0.000572)
        for c in (1, 2, 0):  # G B R
            seq.scan(row[:, c], 0.146432)
            seq.tone(1500, 0.000572)


def scottie1(seq, img):
    seq.tone(1200, 0.009)  # starting sync
    for row in img:
        seq.tone(1500, 0.0015)
        seq.scan(row[:, 1], 0.138240)  # G
        seq.tone(1500, 0.0015)
        seq.scan(row[:, 2], 0.138240)  # B
        seq.tone(1200, 0.009)
        seq.tone(1500, 0.0015)
        seq.scan(row[:, 0], 0.138240)  # R


def pd120(seq, img):
    y, u, v = ycc(img)
    for i in range(0, img.shape[0], 2):
        seq.tone(1200, 0.020)
        seq.tone(1500, 0.00208)
        seq.scan(y[i], 0.1216)
        seq.scan((v[i] + v[i + 1]) / 2, 0.1216)
        seq.scan((u[i] + u[i + 1]) / 2, 0.1216)
        seq.scan(y[i + 1], 0.1216)


def robot36(seq, img):
    y, u, v = ycc(img)
    for i, row in enumerate(img):
        seq.tone(1200, 0.009)
        seq.tone(1500, 0.003)
        seq.scan(y[i], 0.088)
        if i % 2 == 0:
            seq.tone(1500, 0.0045)
            seq.tone(1900, 0.0015)
            seq.scan(v[i], 0.044)
        else:
            seq.tone(2300, 0.0045)
            seq.tone(1900, 0.0015)
            seq.scan(u[i], 0.044)


ENCODERS = {'M1': martin1, 'S1': scottie1, 'PD120': pd120, 'R36': robot36}


def synth(seq, ppm, lead, tail):
    f = np.concatenate(([0.0], np.asarray(seq.f), [0.0]))
    d = np.concatenate(([lead], np.asarray(seq.d), [tail]))
    ends = np.cumsum(d)
    # the sender's clock runs (1+ppm) fast: nominal durations take less real time
    n = int(ends[-1] / (1 + ppm * 1e-6) * FS)
    t_nominal = np.arange(n) / FS * (1 + ppm * 1e-6)
    fi = f[np.searchsorted(ends, t_nominal, side='right').clip(0, len(f) - 1)]
    phase = 2 * np.pi * np.cumsum(fi) / FS
    sig = np.where(fi > 0, np.sin(phase), 0.0)
    return sig


def write_wav(fn, sig):
    import wave
    pcm = (np.clip(sig, -1, 1) * 32767).astype('<i2')
    with wave.open(fn, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(FS)
        w.writeframes(pcm.tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('mode', choices=MODES.keys())
    ap.add_argument('out')
    ap.add_argument('--image')
    ap.add_argument('--ppm', type=float, default=0.0)
    ap.add_argument('--lead', type=float, default=1.0, help='silence before the signal (s)')
    ap.add_argument('--tail', type=float, default=1.0, help='silence after the signal (s)')
    a = ap.parse_args()
    m = MODES[a.mode]
    if a.image:
        img = np.asarray(Image.open(a.image).convert('RGB').resize((m['w'], m['h'])))
    else:
        img = test_pattern(m['w'], m['h'])
    seq = Seq()
    vis(seq, m['vis'])
    ENCODERS[a.mode](seq, img)
    sig = synth(seq, a.ppm, a.lead, a.tail) * 0.5
    write_wav(a.out, sig)
    Image.fromarray(img).save(a.out + '.ref.png')
    print(f'{a.out}: {a.mode} {len(sig) / FS:.1f} s, sender clock {a.ppm:+.0f} ppm')


if __name__ == '__main__':
    main()

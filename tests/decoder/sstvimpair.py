#!/usr/bin/env python3
"""Add radio impairments to a 48 kHz WAV.

  sstvimpair.py in.wav out.wav [options]

  --ppm N            clock difference sender/receiver (resampling, positive: received lines shorter)
  --snr DB           white noise, SNR measured in a 2500 Hz bandwidth
  --qsb PERIOD,DEPTH slow fading: sine shaped, PERIOD seconds, DEPTH dB
  --flutter RATE,DEPTH  fast random fading (Rayleigh-like), RATE Hz, DEPTH dB
  --dropout T,D      signal gone at T seconds for D seconds (only noise remains), may be repeated
  --offset HZ        frequency offset (mistuned SSB)
  --qrm F,DB         interfering carrier at F Hz, level relative to the signal
  --seed N           random seed (default 1), the same options give the same file
"""
import argparse
import wave
import numpy as np
from scipy.signal import hilbert

FS = 48000


def read_wav(fn):
    with wave.open(fn, 'rb') as w:
        assert w.getframerate() == FS and w.getsampwidth() == 2
        x = np.frombuffer(w.readframes(w.getnframes()), '<i2').astype(float) / 32767
        ch = w.getnchannels()
    return x.reshape(-1, ch).mean(axis=1)


def write_wav(fn, x):
    pcm = (np.clip(x, -1, 1) * 32767).astype('<i2')
    with wave.open(fn, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(FS)
        w.writeframes(pcm.tobytes())


def pair(s):
    a, b = s.split(',')
    return float(a), float(b)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('inp')
    ap.add_argument('out')
    ap.add_argument('--ppm', type=float, default=0)
    ap.add_argument('--snr', type=float)
    ap.add_argument('--qsb', type=pair)
    ap.add_argument('--flutter', type=pair)
    ap.add_argument('--dropout', type=pair, action='append', default=[])
    ap.add_argument('--offset', type=float, default=0)
    ap.add_argument('--qrm', type=pair)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    rng = np.random.default_rng(a.seed)

    x = read_wav(a.inp)
    active = np.abs(x) > 1e-4
    sig_power = np.mean(x[active] ** 2) if active.any() else 1e-6

    if a.ppm:
        # a receiver clock that is ppm slow sees the signal compressed
        n = int(len(x) / (1 + a.ppm * 1e-6))
        x = np.interp(np.arange(n) * (1 + a.ppm * 1e-6), np.arange(len(x)), x)
    t = np.arange(len(x)) / FS

    if a.offset:
        x = np.real(hilbert(x) * np.exp(2j * np.pi * a.offset * t))

    gain = np.ones(len(x))
    if a.qsb:
        period, depth = a.qsb
        gain *= 10 ** (-depth / 20 * (0.5 - 0.5 * np.cos(2 * np.pi * t / period)))
    if a.flutter:
        rate, depth = a.flutter
        k = max(1, int(FS / rate))
        g = np.abs(rng.normal(size=len(x) // k + 2) + 1j * rng.normal(size=len(x) // k + 2)) / np.sqrt(2)
        g = np.interp(np.arange(len(x)) / k, np.arange(len(g)), g)
        gain *= 10 ** (np.clip(20 * np.log10(g + 1e-9), -depth, 6) / 20)
    for start, dur in a.dropout:
        gain[(t >= start) & (t < start + dur)] = 0
    x = x * gain

    if a.qrm:
        f, db = a.qrm
        x = x + np.sqrt(2 * sig_power) * 10 ** (db / 20) * np.sin(2 * np.pi * f * t)
    if a.snr is not None:
        noise_power = sig_power / 10 ** (a.snr / 10) * (FS / 2) / 2500
        x = x + rng.normal(scale=np.sqrt(noise_power), size=len(x))

    peak = np.max(np.abs(x))
    if peak > 0.9:
        x *= 0.9 / peak
    write_wav(a.out, x)


if __name__ == '__main__':
    main()

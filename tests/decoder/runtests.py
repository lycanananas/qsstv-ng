#!/usr/bin/env python3
"""Run the SSTV decoder test scenarios and print a result table.

  runtests.py DECODERTEST WORKDIR [--base DECODERTEST] [--only NAME[,NAME]] [--jobs N]

DECODERTEST is the built decodertest binary. Generated signals are cached in WORKDIR.
With --base a second decoder (e.g. built from the previous commit) is run for comparison.
Columns:
  imgs    images produced (1 expected, more means the reception was restarted)
  good    rows of the best image that are in the right place (see sstvmeasure.py)
  noise   remaining difference of those rows
  slant   slant of the best image in pixels (top to bottom)
  offset  horizontal offset of the best image in pixels
"""
import argparse
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sstvmeasure  # noqa: E402

# name, mode, sender ppm, impairments, decoder options
SCENARIOS = [
    ('clean-M1',          'M1',    0,    [],                                  []),
    ('clean-S1',          'S1',    0,    [],                                  []),
    ('clean-PD120',       'PD120', 0,    [],                                  []),
    ('clean-R36',         'R36',   0,    [],                                  []),
    ('ppm+100-M1',        'M1',    100,  [],                                  []),
    ('ppm-100-M1',        'M1',    -100, [],                                  []),
    ('ppm+300-M1',        'M1',    300,  [],                                  []),
    ('ppm+300-S1',        'S1',    300,  [],                                  []),
    ('ppm+300-PD120',     'PD120', 300,  [],                                  []),
    ('ppm+100-M1-noauto', 'M1',    100,  [],                                  ['--autoslant', '0']),
    ('ppm+300-M1-noauto', 'M1',    300,  [],                                  ['--autoslant', '0']),
    ('snr10-M1',          'M1',    0,    ['--snr', '10'],                     []),
    ('snr5-M1',           'M1',    0,    ['--snr', '5'],                      []),
    ('snr3-S1',           'S1',    0,    ['--snr', '3'],                      []),
    ('drop3s-M1',         'M1',    0,    ['--snr', '15', '--dropout', '50,3'], []),
    ('drop8s-M1',         'M1',    0,    ['--snr', '15', '--dropout', '50,8'], []),
    ('drop8s-S1',         'S1',    0,    ['--snr', '15', '--dropout', '60,8'], []),
    ('drop8s-PD120',      'PD120', 0,    ['--snr', '15', '--dropout', '60,8'], []),
    ('qsb20dB-M1',        'M1',    0,    ['--snr', '12', '--qsb', '15,20'],   []),
    ('flutter-M1',        'M1',    0,    ['--snr', '15', '--flutter', '8,15'], []),
    ('offset50-M1',       'M1',    0,    ['--offset', '50'],                  []),
    ('qrm-M1',            'M1',    0,    ['--snr', '15', '--qrm', '1200,-10'], []),
    ('combo-M1',          'M1',    150,  ['--snr', '8', '--dropout', '45,4', '--qsb', '20,10'], []),
    ('combo-S1',          'S1',    -200, ['--snr', '8', '--dropout', '70,5', '--flutter', '5,10'], []),
]


def gen(work, mode, ppm):
    fn = os.path.join(work, f'gen_{mode}_{ppm:+d}.wav')
    if not os.path.exists(fn):
        subprocess.run([sys.executable, os.path.join(HERE, 'sstvgen.py'), mode, fn, '--ppm', str(ppm),
                        '--lead', '2', '--tail', '2'], check=True, stdout=subprocess.DEVNULL)
    return fn


def run(decoder, work, sc, tag):
    name, mode, ppm, impair, dec_opts = sc
    src = gen(work, mode, ppm)
    wav = os.path.join(work, f'{name}.wav')
    if not os.path.exists(wav):
        subprocess.run([sys.executable, os.path.join(HERE, 'sstvimpair.py'), src, wav] + impair, check=True)
    out = os.path.join(work, 'out' + tag, name)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        os.remove(os.path.join(out, f))
    p = subprocess.run([decoder, '--out', out] + dec_opts + [wav], capture_output=True, text=True)
    images = sorted(f for f in os.listdir(out) if f.endswith('.png'))
    best = None
    for f in images:
        r = sstvmeasure.measure(os.path.join(out, f), src + '.ref.png')
        if best is None or r['good'] > best['good']:
            best = r
    nvalid = len([f for f in images if 'invalid' not in f])
    return name, len(images), nvalid, best, p.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('decoder')
    ap.add_argument('work')
    ap.add_argument('--only')
    ap.add_argument('--base')
    ap.add_argument('--jobs', type=int, default=os.cpu_count())
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    scs = SCENARIOS
    if a.only:
        names = a.only.split(',')
        scs = [s for s in SCENARIOS if any(re.fullmatch(n.replace('*', '.*'), s[0]) for n in names)]
    # generate the signals once before running in parallel
    for s in scs:
        gen(a.work, s[1], s[2])
    jobs = [(a.decoder, s, '') for s in scs]
    if a.base:
        jobs += [(a.base, s, '-base') for s in scs]
    with ThreadPoolExecutor(a.jobs) as ex:
        res = list(ex.map(lambda j: run(j[0], a.work, j[1], j[2]), jobs))
    new, base = res[:len(scs)], res[len(scs):]

    def fmt(r):
        name, n, nvalid, best, rc = r
        if best is None:
            return f'{n:4d}    ---     ---     ---    ---'
        return f"{n:4d} {best['good']:6.1f} {best['slant']:+7.1f} {best['offset']:+7.1f} {best['rmse']:6.1f}"

    head = f"{'imgs':>4} {'good%':>6} {'slant':>7} {'offset':>7} {'noise':>6}"
    print(f"{'scenario':20} {head}" + (f"   | base: {head}" if base else ''))
    for i, r in enumerate(new):
        print(f'{r[0]:20} {fmt(r)}' + (f'   |       {fmt(base[i])}' if base else ''))


if __name__ == '__main__':
    main()

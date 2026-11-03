# SSTV decoder tests

Runs the real QSSTV-NG SSTV receiver (sstvRx, syncProcessor, modes, filters) on WAV files,
without GUI. The GUI parts are replaced by the headers in `stubs/`.

    mkdir -p /tmp/dt && cd /tmp/dt && qmake-qt5 ~/git/qsstv/tests/decoder/decodertest.pro && make -j
    ./runtests.py /tmp/dt/decodertest /tmp/sstvwork            # all scenarios
    ./runtests.py /tmp/dt/decodertest /tmp/sstvwork --base /tmp/dt-old/decodertest   # compare two builds

- `sstvgen.py`     reference signal with standard timing (M1, S1, PD120, R36), optional sender clock error
- `sstvimpair.py`  clock error, noise, QSB, flutter, dropouts, frequency offset, QRM
- `sstvmeasure.py` slant, offset and rows in the right place of a decoded image
- `decodertest`    decodes a WAV (48 kHz 16 bit) to PNG files, `--log SLANTAD,SYNCLOST,SYNCREJ` shows the decoder log

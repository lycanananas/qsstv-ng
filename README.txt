QSSTV-NG - fork of QSSTV (Johan Maes, ON4QZ)
https://github.com/lycanananas/qsstv-ng

Changes compared to QSSTV:
- Qt Multimedia sound interface (Options -> Configure -> Sound -> Qt Multimedia):
  individual input and output devices can be selected, also with PipeWire / PulseAudio.
  Input left and right channels are mixed to mono, output sends the same signal on
  left and right (unless "PTT tone on other audio channel" is enabled).
- Settings are stored separately (~/.config/QSSTV-NG/), on the first start the
  settings of QSSTV are copied.

The full manual of the original QSSTV:
http://users.telenet.be/on4qz/qsstv/manual/index.html

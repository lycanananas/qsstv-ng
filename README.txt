QSSTV-NG - fork of QSSTV (Johan Maes, ON4QZ)
https://github.com/lycanananas/qsstv-ng

Changes compared to QSSTV:
- Qt Multimedia sound interface (Options -> Configure -> Sound -> Qt Multimedia):
  individual input and output devices can be selected, also with PipeWire / PulseAudio.
  Input left and right channels are mixed to mono, output sends the same signal on
  left and right (unless "PTT tone on other audio channel" is enabled).
- Settings are stored separately (~/.config/QSSTV-NG/), on the first start the
  settings of QSSTV are copied.

Building the Arch Linux package:
  mkdir -p /tmp/qsstv-ng-pkg && cp PKGBUILD /tmp/qsstv-ng-pkg/
  cd /tmp/qsstv-ng-pkg && makepkg -si
or build the package file first and install it with pacman:
  makepkg -s
  sudo pacman -U qsstv-ng-git-*.pkg.tar.zst
The PKGBUILD builds the latest commit from GitHub and installs qsstv-ng-git,
which replaces the qsstv package.
To package this checkout instead (including uncommitted changes), run in this directory:
  QSSTV_LOCAL=1 makepkg -Cfsi
For a local build without packaging: qmake-qt5 && make

The full manual of the original QSSTV:
http://users.telenet.be/on4qz/qsstv/manual/index.html

And yes it is AI slopped fix for QSSTV. There is no better app for SSTV than QSSTV.
  ^ - I wrote this by my self not an AI ;) (Only this line of text). TNX ON4QZ for this software!
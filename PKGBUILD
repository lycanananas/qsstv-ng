#Maintainer: lycanananas <adrian@grzeca.me>
#Contributor: Alexander Fasching <fasching.a91@gmail.com>
#Contributor: Andreas Schreiner <andreas.schreiner@sonnenmulde.at>
#Contributor: Bob Finch <w9ya@qrparci.net>

# Default: build the latest commit from GitHub.
# Local build of the working tree next to this PKGBUILD (including uncommitted changes):
#   QSSTV_LOCAL=1 makepkg -Cfsi
_local=${QSSTV_LOCAL:-0}

_pkgname=qsstv-ng
pkgname=${_pkgname}-git
pkgver=10.0.0.r1.g7bb4a22
pkgrel=1
pkgdesc="Radio Slow-Scan TV for qt (QSSTV fork with Qt Multimedia audio device selection)"
url="https://github.com/lycanananas/qsstv-ng"
depends=('qt5-base' 'qt5-multimedia' 'libpulse' 'v4l-utils' 'hamlib' 'fftw' 'openjpeg2' 'alsa-lib')
makedepends=('git')
provides=("${_pkgname}" 'qsstv')
conflicts=("${_pkgname}" 'qsstv')
replaces=('qsstv')
optdepends=('pipewire-pulse: PipeWire as audio server for the Qt Multimedia sound interface'
            'pulseaudio: PulseAudio as audio server for the Qt Multimedia sound interface')
arch=('i686' 'x86_64')
license=('GPL-2.0-or-later')
options=('!debug')

if [[ $_local == 1 ]]; then
  source=()
  sha512sums=()
else
  source=("${_pkgname}::git+https://github.com/lycanananas/qsstv-ng.git")
  sha512sums=('SKIP')
fi

_gitdir() {
  if [[ $_local == 1 ]]; then echo "$startdir"; else echo "$srcdir/${_pkgname}"; fi
}

# short commit hash shown in the window title, "+local" when there are uncommitted changes
_commit() {
  local c
  c=$(git -C "$(_gitdir)" rev-parse --short=7 HEAD)
  if [[ $_local == 1 ]] && [[ -n $(git -C "$startdir" status --porcelain) ]]; then
    c+="+local"
  fi
  echo "$c"
}

pkgver() {
  cd "$(_gitdir)"
  # tag v10.0.0 -> 10.0.0.r<commits since tag>.g<hash>, without tags 10.0.0.r<commit count>.g<hash>
  ( set -o pipefail
    git describe --long --tags --abbrev=7 2>/dev/null | sed 's/^v//;s/\([^-]*-g\)/r\1/;s/-/./g' ||
    printf "10.0.0.r%s.g%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short=7 HEAD)"
  )
}

prepare() {
  [[ $_local == 1 ]] || return 0
  # copy the working tree: tracked and new files, without ignored build output
  rm -rf "$srcdir/${_pkgname}"
  mkdir -p "$srcdir/${_pkgname}"
  ( cd "$startdir" && git ls-files -z -co --exclude-standard |
      tar --null --ignore-failed-read -T - -cf - ) | tar -xf - -C "$srcdir/${_pkgname}"
}

build() {
  cd "$srcdir/${_pkgname}"
  qmake-qt5 PREFIX=/usr/ GITCOMMIT="$(_commit)"
  make $MAKEFLAGS
}

package() {
  cd "$srcdir/${_pkgname}"
  mkdir -p "$pkgdir/usr/bin/"
  make INSTALL_ROOT="$pkgdir" install

  install -D -m644 "${_pkgname}.desktop" "$pkgdir/usr/share/applications/${_pkgname}.desktop"
  install -D -m644 "icons/qsstv.png" "$pkgdir/usr/share/pixmaps/${_pkgname}.png"
}

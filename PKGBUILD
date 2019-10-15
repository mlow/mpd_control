pkgname=mpd_control
pkgver=0.1.0
pkgrel=1
pkgdesc="An i3blocks blocklet for MPD"
arch=('i686' 'x86_64')
license=('MIT')
depends=('libmpdclient')
source=("Makefile" "mpd_control.c")
sha256sums=('0b6a75b66d313f47ed738ec6fa8d58644b1870e1967a82f42854cc7605cf2437'
            '89f76a3319de3f494f9f01bd1994adc8afd6b79530ab23c86737bf3aabf34bc8')
provides=('mpd_control')

build() {
  make
}

package() {
  install -Dm755 mpd_control ${pkgdir}/usr/bin/mpd_control
}

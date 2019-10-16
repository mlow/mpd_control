pkgname=mpd_control
pkgver=0.1.1
pkgrel=1
pkgdesc="An i3blocks blocklet for MPD"
arch=('i686' 'x86_64')
license=('MIT')
depends=('libmpdclient')
source=("Makefile" "mpd_control.c")
sha256sums=('15a80555d9f7989016fe14a9a962ff2d7d7196fb170014338400b8e3cf36891b'
            'd76724e59113d89eae522bb216545b3c358670a9ba4139cb4a3f363eac2032d2')
provides=('mpd_control')

build() {
  make
}

package() {
  install -Dm755 mpd_control ${pkgdir}/usr/bin/mpd_control
}

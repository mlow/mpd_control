pkgname=mpd_control
pkgver=0.1.3
pkgrel=1
pkgdesc="An i3blocks blocklet for MPD"
arch=('i686' 'x86_64')
license=('MIT')
depends=(libmpdclient glib2)
source=("Makefile" "mpd_control.c")
sha256sums=('15a80555d9f7989016fe14a9a962ff2d7d7196fb170014338400b8e3cf36891b'
            '534ba6966e535e88f435c61ec8171d74bf811d51553523d727fd602042e204a6')
provides=('mpd_control')

build() {
  make
}

package() {
  install -Dm755 mpd_control ${pkgdir}/usr/bin/mpd_control
}

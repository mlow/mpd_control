pkgname=mpd_control
pkgver=0.1.2
pkgrel=1
pkgdesc="An i3blocks blocklet for MPD"
arch=('i686' 'x86_64')
license=('MIT')
depends=('libmpdclient')
source=("Makefile" "mpd_control.c")
sha256sums=('15a80555d9f7989016fe14a9a962ff2d7d7196fb170014338400b8e3cf36891b'
            '41a4ec1fa03aaa08e4c5f64410445a2c2c777d601e4be518951e183ac3b0c72d')
provides=('mpd_control')

build() {
  make
}

package() {
  install -Dm755 mpd_control ${pkgdir}/usr/bin/mpd_control
}

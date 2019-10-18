pkgname=mpd_control
pkgver=0.3.0
pkgrel=1
pkgdesc="An i3blocks blocklet for MPD"
arch=('i686' 'x86_64')
license=('MIT')
depends=(libmpdclient glib2)
source=("Makefile" "mpd_control.c")
sha256sums=('73ab8749ca8f6931e123b05643bb79c82629fee36439e17b1abb8ae146425af9'
            '835d4425cf622c7cc14d4ff7951278ba3b38fdd1ef8f14728427b9fa7ef7653e')
provides=('mpd_control')

build() {
  make
}

package() {
  install -Dm755 mpd_control ${pkgdir}/usr/bin/mpd_control
}

pkgname=mpd_control
pkgver=0.2.0
pkgrel=1
pkgdesc="An i3blocks blocklet for MPD"
arch=('i686' 'x86_64')
license=('MIT')
depends=(libmpdclient glib2)
source=("Makefile" "mpd_control.c")
sha256sums=('15a80555d9f7989016fe14a9a962ff2d7d7196fb170014338400b8e3cf36891b'
            'b5452e5b8019d80d9fc584ef9b89a483b3fa4bfc095faf9eb8bbadb581fac9c6')
provides=('mpd_control')

build() {
  make
}

package() {
  install -Dm755 mpd_control ${pkgdir}/usr/bin/mpd_control
}

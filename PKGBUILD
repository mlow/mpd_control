pkgname=mpd_control
pkgver=0.0.1
pkgrel=1
pkgdesc="An i3blocks blocklet for MPD" 
arch=('i686' 'x86_64')
license=('MIT')
depends=('libmpdclient')
source=("Makefile" "mpd_control.c")
sha256sums=('d75fcdd75f83daec43912d6ef243974b181ea9fe98ba2c43160b9e1541abb283'
            'fd0383ecc78b393bcb09f6666c3dff20846bf7be047ae7a20f612a84bb9e57fd')
provides=('mpd_control')

build() {
  make
}

package() {
  install -Dm755 mpd_control ${pkgdir}/usr/bin/mpd_control
}

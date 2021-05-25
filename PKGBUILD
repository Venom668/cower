pkgname=cower
pkgver=18
pkgrel=1
pkgdesc="A simple AUR agent with a pretentious name"
arch=('any')
url="https://github.com/Venom668/cower"
license=('MIT')
depends=('curl' 'pacman' 'yajl')
makedepends=('perl')
source=("git+https://github.com/Venom668/cower.git")
sha256sums=('SKIP')

build() {
  cd "$pkgname"

  make
  sed '/^$/q' src/cower.c > LICENSE
}

package() {
  cd "$pkgname"

  make PREFIX=/usr DESTDIR="$pkgdir" install
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
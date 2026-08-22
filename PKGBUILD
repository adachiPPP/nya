pkgname=nya
pkgver=2.8.0
pkgrel=1
pkgdesc="A pacman-compatible package manager written in C, with AUR, nix and flatpak support"
arch=('x86_64' 'aarch64')
url="https://github.com/adachip/nya"
license=('custom')
depends=('curl' 'zlib' 'zstd' 'xz')
makedepends=('pkg-config')
source=()
sha256sums=()

build() {
  cd "$startdir"
  make
}

check() {
  cd "$startdir"
  make test
}

package() {
  cd "$startdir"
  install -Dm755 nya "$pkgdir/usr/bin/nya"
}

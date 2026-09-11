# Maintainer: archpaper user
pkgname=archpaper
pkgver=0.1.0
pkgrel=1
pkgdesc="Lightweight Wayland wallpaper manager with Qt6 GUI for Arch Linux"
arch=('x86_64')
url="https://github.com/staFF6773/archpaper"
license=('GPL-3.0-or-later')
depends=('swaybg' 'qt6-base')
optdepends=('hyprpaper: alternative backend on Hyprland'
            'awww: efficient animated/GIF wallpapers on Wayland'
            'mpvpaper: video wallpapers on Wayland'
            'wallust: color scheme generation'
            'ffmpeg: video thumbnails and oversized wallpaper conversion'
            'ffmpegthumbnailer: fast video thumbnails')
makedepends=('cmake' 'gcc' 'make')

build() {
    cmake -B build -S "$startdir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build
}

package() {
    DESTDIR="$pkgdir" cmake --install build
}

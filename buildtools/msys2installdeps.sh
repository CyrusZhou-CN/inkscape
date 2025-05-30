#!/usr/bin/env bash
set -e
# -------------------------------------------------------------------------------
# This script installs all dependencies required for building Inkscape with MSYS2.
#
# See https://wiki.inkscape.org/wiki/Compiling_Inkscape_on_Windows_with_MSYS2 for
# detailed instructions.
#
# The following instructions assume you are building for the standard x86_64 processor architecture,
# which means that you use the UCRT64 variant of msys2.
# Else, replace UCRT64 with the appropriate variant for your architecture.
#
# To run this script, execute it once on an UCRT64 shell, i.e.
#    - use the "MSYS2 UCRT64" shortcut in the start menu or
#    - run "ucrt64.exe" in MSYS2's installation folder
#
# MSYS2 and installed libraries can be updated later by executing
#   pacman -Syu
# -------------------------------------------------------------------------------

# select if you want to build 32-bit (i686), 64-bit (x86_64), or both
case "$MSYSTEM" in
  MINGW32)
    ARCH=mingw-w64-i686
    ;;
  MINGW64)
    ARCH=mingw-w64-x86_64
    ;;
  UCRT64)
    ARCH=mingw-w64-ucrt-x86_64
    ;;
  CLANG64)
    ARCH=mingw-w64-clang-x86_64
    ;;
  CLANGARM64)
    ARCH=mingw-w64-clang-aarch64
    ;;
  *)
    ARCH={mingw-w64-i686,mingw-w64-x86_64}
    ;;
esac

# set default options for invoking pacman (in CI this variable is already set globally)
if [ -z $CI ]; then
    PACMAN_OPTIONS="--needed --noconfirm"
fi

# sync package databases
pacman -Sy

# install basic development system, compiler toolchain and build tools
eval pacman -S $PACMAN_OPTIONS \
git \
base-devel \
$ARCH-toolchain \
$ARCH-autotools \
$ARCH-cmake \
$ARCH-meson \
$ARCH-ninja \
$ARCH-ccache

# install Inkscape dependencies (required)
eval pacman -S $PACMAN_OPTIONS \
$ARCH-double-conversion \
$ARCH-gc \
$ARCH-gsl \
$ARCH-libxslt \
$ARCH-boost \
$ARCH-gtk4 \
$ARCH-gtk-doc \
$ARCH-gtkmm4

# install packaging tools (required for dist-win-* targets)
eval pacman -S $PACMAN_OPTIONS \
$ARCH-7zip \
$ARCH-nsis \
$ARCH-ntldd

# install Inkscape dependencies (optional)
eval pacman -S $PACMAN_OPTIONS \
$ARCH-poppler \
$ARCH-potrace \
$ARCH-libcdr \
$ARCH-libvisio \
$ARCH-libwpg \
$ARCH-gtksourceview5 \
$ARCH-libjxl \
$ARCH-enchant

# install Python and modules used by Inkscape
eval pacman -S $PACMAN_OPTIONS \
$ARCH-python \
$ARCH-python-pip \
$ARCH-python-lxml \
$ARCH-python-numpy \
$ARCH-python-cssselect \
$ARCH-python-webencodings \
$ARCH-python-tinycss2 \
$ARCH-python-pillow \
$ARCH-python-six \
$ARCH-python-gobject \
$ARCH-python-pyserial \
$ARCH-python-coverage \
$ARCH-python-packaging \
$ARCH-python-zstandard \
$ARCH-scour

# install modules needed by extensions manager and clipart importer
eval pacman -S $PACMAN_OPTIONS \
$ARCH-python-appdirs \
$ARCH-python-beautifulsoup4 \
$ARCH-python-filelock \
$ARCH-python-msgpack \
$ARCH-python-cachecontrol \
$ARCH-python-idna \
$ARCH-python-urllib3 \
$ARCH-python-chardet \
$ARCH-python-certifi \
$ARCH-python-requests

# install packages for testing Inkscape
eval pacman -S $PACMAN_OPTIONS \
$ARCH-gtest

# install Python modules not provided as MSYS2/MinGW packages
PACKAGES=""
for arch in $(eval echo $ARCH); do
  case ${arch} in
    mingw-w64-i686)
      #/mingw32/bin/pip3 install --upgrade ${PACKAGES}
      ;;
    mingw-w64-x86_64)
      #/mingw64/bin/pip3 install --upgrade ${PACKAGES}
      ;;
    mingw-w64-ucrt-x86_64)
      #/ucrt64/bin/pip3 install --upgrade ${PACKAGES}
      ;;
    mingw-w64-clang-x86_64)
      #/clang64/bin/pip3 install --upgrade ${PACKAGES}
      ;;
    mingw-w64-clang-aarch64)
      #/clangarm64/bin/pip3 install --upgrade ${PACKAGES}
      ;;
  esac
done

# compile graphicsmagick for current arch as MSYS2 packaged one is buggy
(
  cd /tmp
  wget https://downloads.sourceforge.net/project/graphicsmagick/graphicsmagick/1.3.43/GraphicsMagick-1.3.43.tar.xz
  tar xJf GraphicsMagick-1.3.43.tar.xz
  cd GraphicsMagick-1.3.43
  ./configure --enable-shared && make && make install
)

echo "Done :-)"

#!/bin/sh
set -e

# Resolve the repo root regardless of where this script is invoked from,
# so the toolchain paths baked into musl-gcc's specs file stay valid even
# if the repo gets moved/cloned to a different location.
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$ROOT_DIR/musl-1.2.6"
tar -xf "$ROOT_DIR/third-party/musl-1.2.6.tar.gz" -C "$ROOT_DIR/musl-1.2.6"
cd "$ROOT_DIR/musl-1.2.6/musl-1.2.6"

./configure \
  --target=i386 \
  --prefix="$ROOT_DIR/sysroot" \
  --disable-shared \
  CC=gcc \
  AR=ar \
  RANLIB=ranlib \
  CFLAGS="-m32 -fno-stack-protector -fno-builtin"

make -j"$(nproc)"
make install

# musl's generated specs file omits "-m elf_i386" in its *link section. That's
# fine for a real i386 cross-toolchain, but our CC is a native x86_64 gcc used
# in -m32 mode, so its linker defaults to elf_x86_64 and rejects musl's i386
# libc.a as "incompatible" unless we force the emulation explicitly.
sed -i 's/^-dynamic-linker /-m elf_i386 -dynamic-linker /' "$ROOT_DIR/sysroot/lib/musl-gcc.specs"

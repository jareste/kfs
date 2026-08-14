mkdir -p musl-1.2.6
tar -xf third-party/musl-1.2.6.tar.gz -C musl-1.2.6
cd musl-1.2.6/musl-1.2.6

./configure \
  --target=i386 \
  --prefix=$PWD/../sysroot \
  --disable-shared \
  CC=gcc \
  AR=ar \
  RANLIB=ranlib \
  CFLAGS="-m32 -fno-stack-protector -fno-builtin"
  
make -j$(nproc)
make install

mkdir -p musl-1.2.4
untar -xf third-party/musl-1.2.4.tar.gz -C musl-1.2.4
cd musl-1.2.4

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

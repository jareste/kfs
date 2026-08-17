#!/bin/sh
set -e

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ ! -f "$ROOT_DIR/sysroot/lib/musl-gcc.specs" ]; then
	"$ROOT_DIR/install_musl.sh"
fi

if [ ! -f "$ROOT_DIR/sysroot/include/linux/capability.h" ]; then
	tar -xzf "$ROOT_DIR/third-party/linux-uapi-headers-i386.tar.gz" -C "$ROOT_DIR/sysroot/include"
fi

cat > "$ROOT_DIR/sysroot/bin/musl-gcc-i386" <<EOF
#!/bin/sh
exec "$ROOT_DIR/sysroot/bin/musl-gcc" -m32 -static "\$@"
EOF
chmod +x "$ROOT_DIR/sysroot/bin/musl-gcc-i386"

rm -rf "$ROOT_DIR/busybox-1.36.1"
tar -xf "$ROOT_DIR/third-party/busybox-1.36.1.tar.bz2" -C "$ROOT_DIR"
cd "$ROOT_DIR/busybox-1.36.1"

cp "$ROOT_DIR/third-party/busybox-1.36.1.config" .config
yes "" | make oldconfig >/dev/null

make CC="$ROOT_DIR/sysroot/bin/musl-gcc-i386" -j"$(nproc)"

cp "$ROOT_DIR/busybox-1.36.1/busybox" "$ROOT_DIR/busybox"

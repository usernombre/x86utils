#!/bin/sh
# Build the x86utils Python bindings (python-cffi/_pyx86utils.so)
# - GCC and Make are required to build all C libraries
# - autoconf, automake and libtool are required to build udis86
# - python2 headers and cffi required to build the bindings themselves
set -eu

ROOT=$(realpath "$(dirname "$0")/..")
BUILD=$ROOT/linux/build
LIB=$BUILD/lib
CC=${CC:-gcc}
CFLAGS="${CFLAGS:--O2 -g} -fPIC -std=gnu99"
PYTHON=${PYTHON:-python2}
JOBS=${JOBS:-$(nproc)}

mkdir -p "$BUILD/yasm" "$BUILD/udis86" "$BUILD/obj" "$LIB"

# Build yasm
cd "$BUILD/yasm"
[ -f Makefile ] || "$ROOT/yasm-1.3.0/configure" CC="$CC" CFLAGS="$CFLAGS"
make -j"$JOBS" libyasm.a
cp libyasm.a "$LIB/"
echo "Built: $LIB/libyasm.a"


# Build udis86
cd "$ROOT/udis86-1.7.2"
[ -f configure ] || ./autogen.sh
cd "$BUILD/udis86"
[ -f Makefile ] || "$ROOT/udis86-1.7.2/configure" CC="$CC" CFLAGS="$CFLAGS" PYTHON="$PYTHON"
make -j"$JOBS"
cp libudis86/.libs/libudis86.a "$LIB/"
echo "Built: $LIB/libudis86.a"

# Build custom source files
for s in assembler instruction; do
    $CC $CFLAGS -I"$ROOT/x86utils" -I"$ROOT/yasm-1.3.0" -I"$BUILD/yasm" \
        -I"$ROOT/udis86-1.7.2" -c "$ROOT/x86utils/$s.c" -o "$BUILD/obj/$s.o"
done
ar rcs "$LIB/libx86utils.a" "$BUILD/obj"/*.o
echo "Built: $LIB/libx86utils.a"

# Build Python bindings
cd "$ROOT/python-cffi"
"$PYTHON" setup.py build_ext --inplace
echo "Built: $ROOT/python-cffi/_pyx86utils.so"

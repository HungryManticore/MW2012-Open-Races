#!/bin/sh
# Builds dist/OpenRaces.asi with a mingw-w64 toolchain for 32-bit Windows
# (i686-w64-mingw32-gcc, or llvm-mingw's i686-w64-mingw32-clang).
# Override with CC=... and WINDRES=... if your toolchain uses other names.
set -e
cd "$(dirname "$0")"
if [ -z "$CC" ]; then
  if command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then CC=i686-w64-mingw32-gcc; else CC=i686-w64-mingw32-clang; fi
fi
WINDRES=${WINDRES:-i686-w64-mingw32-windres}
mkdir -p build dist
"$WINDRES" -O coff -i src/OpenRaces.rc -o build/OpenRaces.res.o
"$CC" -O2 -mno-sse -fno-stack-protector -Wall -Wno-unused-function -Wno-unused-variable \
  -shared -static-libgcc -s -o dist/OpenRaces.asi src/OpenRaces.c build/OpenRaces.res.o -luser32 -lkernel32
echo "built dist/OpenRaces.asi"

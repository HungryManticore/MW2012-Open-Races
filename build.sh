#!/bin/sh
# Builds dist/OpenRaces.asi with clang + lld (no Windows SDK or CRT needed).
set -e
LLVM=${LLVM:-/usr/lib/llvm-18/bin}
cd "$(dirname "$0")"
mkdir -p build dist
"$LLVM/llvm-dlltool" -m i386 -k -d src/kernel32.def -l build/kernel32.lib
"$LLVM/llvm-dlltool" -m i386 -d src/user32.def -l build/user32.lib
clang -target i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector -mno-sse \
  -Wall -Wno-unused-function -Wno-unused-variable -c src/OpenRaces.c -o build/OpenRaces.obj
"$LLVM/lld-link" /dll /entry:DllMain /nodefaultlib /subsystem:windows /machine:x86 /safeseh:no \
  build/OpenRaces.obj build/kernel32.lib build/user32.lib /out:dist/OpenRaces.asi
echo "built dist/OpenRaces.asi"

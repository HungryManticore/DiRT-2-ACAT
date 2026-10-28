#!/bin/sh
# Builds D2ACAT.asi (32-bit DLL) with clang + lld-link. No CRT or Windows SDK needed.
# Output goes to build/.
set -e
cd "$(dirname "$0")"
mkdir -p build
llvm-dlltool -m i386 -d src/kernel32.def -l build/kernel32.lib -k
clang --target=i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector \
      -mno-sse -c src/D2ACAT.c -o build/D2ACAT.obj
lld-link /dll /nodefaultlib /entry:DllMain /subsystem:windows /machine:x86 \
         /dynamicbase /safeseh:no \
         build/D2ACAT.obj build/kernel32.lib /out:build/D2ACAT.asi
cp D2ACAT.ini build/
echo "built build/D2ACAT.asi"

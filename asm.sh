#!/usr/bin/sh

src=$1
target=$(echo "$src" | sed 's/\.s$//')
obj="$target"".o"

#gcc -o rt rt.s -lc
as -o "$obj" "$src"
ld -o "$target" "$obj" -dynamic-linker /lib/ld-linux-x86-64.so.2 -lc
rm -f "$obj"

#!/bin/sh

cflags='-D_POSIX_C_SOURCE=200809L -Wextra -Wall -Wswitch-enum -ggdb -rdynamic -std=c11'
lflags=
cc=gcc
target='sly'

compile() {
	"$cc" $cflags $lflags -o "$target" *.c
}

asm() {
	"$cc" $cflags -S -o main3.s main.c
	"$cc" $cflags -S sly_types.c
}

clean() {
	rm -f *.o
}

clean_all() {
	clean
	rm -f "$target"
}

case "$1" in
	"asm")
		asm
		;;
	"clean")
		clean_all
		;;
	"run")
		compile && clean && "./$target"
		;;
	"test")
		"./$target" *.scm
		;;
	*)
		compile
		clean
		;;
esac

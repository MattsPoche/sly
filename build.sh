#!/bin/sh

cflags='-D_POSIX_C_SOURCE=200809L -Wextra -Wall -Wswitch-enum -ggdb -rdynamic -std=c11'
lflags=
cc=gcc
target='sly'

set -e

compile() {
	"$cc" $cflags $lflags -o "$target" *.c
}

clean() {
	rm -f *.o
}

clean_all() {
	clean
	rm -f "$target"
}

case "$1" in
	"clean")
		clean_all
		;;
	"run")
		(set -x; compile) && clean && (set -x; "./$target")
		;;
	"test")
		(set -x; "./$target" ./test/*.scm)
		;;
	*)
		(set -x; compile)
		clean
		;;
esac

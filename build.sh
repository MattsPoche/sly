#!/bin/sh

cflags='-Werror -Wextra -pedantic -Wall -Wswitch-enum -ggdb -std=c11'
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
	"build-test"|"bt")
		(set -x; compile && "./$target" ./test/*.scm)
		;;
	"test")
		(set -x; "./$target" ./test/*.scm)
		;;
	"tags"|"tag")
		(set -x; ctags -e *.c *.h)
		;;
	*)
		(set -x; compile)
		clean
		;;
esac

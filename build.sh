#!/bin/sh

cflags='-Werror -Wextra -pedantic -Wall -Wswitch-enum -ggdb -std=c11 -DUSE_SLY_ALLOC'
cc=gcc
target='sly'

set -e

opt="$1"
comp='compile'

if [ "$1" = '--no-rl' ]
then
	comp='compile_noreadline'
	opt="$2"
fi


compile_noreadline() {
	"$cc" $cflags $lflags -DNO_READLINE -o "$target" *.c
}

compile() {
	"$cc" $cflags `pkg-config --cflags --libs readline` -o "$target" *.c
}

clean() {
	rm -f *.o
}

clean_all() {
	clean
	rm -f "$target"
}

case "$opt" in
	"clean")
		clean_all
		;;
	"run")
		(set -x; "$comp") && clean && (set -x; "./$target")
		;;
	"build-test"|"bt")
		(set -x; "$comp" && "./$target" ./test/*.scm)
		;;
	"test")
		(set -x; "./$target" ./test/*.scm)
		;;
	"tags"|"tag")
		(set -x; ctags -e *.c *.h)
		;;
	*)
		(set -x; "$comp")
		clean
		;;
esac

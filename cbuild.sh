#!/usr/bin/sh

cfile="test.sly.c"
target="test_prg"

case "$1" in
	"--only-c")
		gcc -o "$target" "$cfile" && ./test_prg ;;
	"--clean")
		rm -f "$target" "$cfile" ;;
	*)
		rm -f "$target" "$cfile"
		make -k && ./sly --expand test/test.sly \
			&& gcc -o "$target" "$cfile" && ./test_prg
		;;
esac

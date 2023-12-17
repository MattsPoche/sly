#!/usr/bin/sh

cfile="test.sly.c"
libfile="test.sly.o"
target="test_prg"
lib="test.so"

clean () {
	rm -f "$target" "$cfile" "$libfile"
}

compile () {
	clean
	make -k && ./sly --expand test/test.sly \
		&& gcc -o "$target" "$cfile" && ./test_prg
}

compile_lib () {
	clean
	make -k && ./sly --expand test/test.sly \
		&& gcc -fPIC -shared -o "$lib" "$cfile"
}

case "$1" in
	"--only-c") gcc -o "$target" "$cfile" && ./test_prg ;;
	"--clean")  clean ;;
	"--lib")    compile_lib ;;
	*)          compile ;;
esac

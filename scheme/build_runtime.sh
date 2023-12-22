#!/usr/bin/sh

warnings='-Wmissing-prototypes -Wextra -pedantic -Wall -Wswitch-enum'
cflags="-ggdb -std=c11 $warnings"
cdir=$(dirname "$0")
runtime="$cdir/scm_runtime.c"
target="$cdir/scm_runtime.o"

# gcc -c -ggdb -o scheme/scm_runtime.o scheme/scm_runtime.c

if [ "$1" = "-s" ]; then
	# shellcheck disable=SC2086 # Intended splitting of cflags
	gcc -fPIC -c $cflags -o "$target" "$runtime"
else
	# shellcheck disable=SC2086 # Intended splitting of cflags
	gcc -c $cflags -o "$target" "$runtime"
fi

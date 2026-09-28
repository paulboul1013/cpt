#!/bin/sh
set -eu
binary=${1:-./tests/geometry-test}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
for form in raw flate array; do
    "$binary" "tests/fixtures/geometry-$form.pdf" > "$tmp/stdout" 2> "$tmp/stderr"
    cmp tests/golden/geometry.stdout "$tmp/stdout"
    test ! -s "$tmp/stderr"
done
"$binary" tests/fixtures/visual-m7-text.pdf > "$tmp/stdout" 2> "$tmp/stderr"
cmp tests/golden/visual-m8-geometry.stdout "$tmp/stdout"
test ! -s "$tmp/stderr"
for fixture in geometry-later-failure rotate-negative rotate-360; do
    status=0
    "$binary" "tests/fixtures/$fixture.pdf" > "$tmp/stdout" 2> "$tmp/stderr" || status=$?
    case "$fixture" in geometry-later-failure) test "$status" -eq 3;; *) test "$status" -eq 4;; esac
    test ! -s "$tmp/stdout"
    test -s "$tmp/stderr"
done
printf '%s\n' 'geometry fixtures: raw/Flate/array, visual trace, and atomic failures passed'

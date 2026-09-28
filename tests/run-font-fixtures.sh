#!/bin/sh
# M9 staged font/decode probe goldens. The raw geometry lines produced through
# the real font adapter must equal the M8 (test Courier adapter) goldens.
set -eu
binary=${1:-./tests/font-text-test}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
for form in raw flate array; do
    "$binary" "tests/fixtures/geometry-$form.pdf" > "$tmp/stdout" 2> "$tmp/stderr"
    cmp tests/golden/font-geometry.stdout "$tmp/stdout"
    test ! -s "$tmp/stderr"
    grep -v '"decode":' "$tmp/stdout" > "$tmp/raw"
    cmp tests/golden/geometry.stdout "$tmp/raw"
done
"$binary" tests/fixtures/visual-m7-text.pdf > "$tmp/stdout" 2> "$tmp/stderr"
grep -v '"decode":' "$tmp/stdout" > "$tmp/raw"
cmp tests/golden/visual-m8-geometry.stdout "$tmp/raw"
test ! -s "$tmp/stderr"
for fixture in font-winansi font-pages; do
    "$binary" "tests/fixtures/$fixture.pdf" > "$tmp/stdout" 2> "$tmp/stderr"
    cmp "tests/golden/$fixture.stdout" "$tmp/stdout"
    test ! -s "$tmp/stderr"
done
# A comma-decimal locale must not change diagnostic numbers (the probe calls
# setlocale(LC_ALL, ""); dumps use a thread-local C numeric locale).
comma=$(locale -a 2>/dev/null | grep -E '^(de_DE|fr_FR|nl_NL)' | head -n 1 || true)
if [ -n "$comma" ]; then
    LC_ALL=$comma "$binary" tests/fixtures/font-winansi.pdf > "$tmp/stdout"
    cmp tests/golden/font-winansi.stdout "$tmp/stdout"
else
    printf '%s\n' 'font fixtures: no comma-decimal locale installed; locale check skipped'
fi
for fixture in font-later-failure font-missing-resource font-type3-empty font-differences \
               geometry-later-failure rotate-negative; do
    status=0
    "$binary" "tests/fixtures/$fixture.pdf" > "$tmp/stdout" 2> "$tmp/stderr" || status=$?
    case "$fixture" in
        font-missing-resource|geometry-later-failure) test "$status" -eq 3;;
        *) test "$status" -eq 4;;
    esac
    test ! -s "$tmp/stdout"
    cmp "tests/golden/$fixture.font.stderr" "$tmp/stderr"
done
printf '%s\n' 'font fixtures: real adapter geometry, WinAnsi/UTF-8 traces, and atomic failures passed'

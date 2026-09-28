#!/bin/sh
# M11 CLI behaviour: argument handling, --help/--version, -o atomic output and
# the header/trailer/object dump modes.
set -u
binary=${1:-./pdftext}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
failures=0
fail() { printf 'cli test failed: %s\n' "$1" >&2; failures=$((failures + 1)); }
# run NAME EXPECTED_STATUS ARGS...: captures stdout/stderr into $tmp/out,err
run() {
    name=$1; want=$2; shift 2
    status=0
    "$binary" "$@" > "$tmp/out" 2> "$tmp/err" || status=$?
    [ "$status" -eq "$want" ] || fail "$name: expected exit $want, got $status"
}
usage_case() {
    name=$1; shift
    run "$name" 1 "$@"
    [ ! -s "$tmp/out" ] || fail "$name: stdout must be empty"
    grep -q '^usage:' "$tmp/err" || fail "$name: usage missing on stderr"
}

usage_case no-args
usage_case unknown --bogus tests/hello.pdf
usage_case two-files tests/hello.pdf tests/hello.pdf
usage_case two-modes --dump-pages --dump-xref tests/hello.pdf
usage_case dump-object-missing --dump-object
usage_case dump-object-zero --dump-object 0 tests/hello.pdf
usage_case dump-object-text --dump-object 3x tests/hello.pdf
usage_case dump-object-sign --dump-object +3 tests/hello.pdf
usage_case o-with-mode -o "$tmp/x.txt" --dump-pages tests/hello.pdf
usage_case o-twice -o "$tmp/a" -o "$tmp/b" tests/fixtures/font-pages.pdf
usage_case o-missing tests/fixtures/font-pages.pdf -o
usage_case help-extra --help tests/hello.pdf

run help 0 --help
grep -q '^usage:' "$tmp/out" && grep -q -- '--dump-text-items' "$tmp/out" && [ ! -s "$tmp/err" ] || fail "help output"
run version 0 --version
[ "$(cat "$tmp/out")" = "pdftext 1.0.0" ] && [ ! -s "$tmp/err" ] || fail "version output"

# Text to stdout equals -o output; -o leaves stdout empty and no temp files.
run stdout 0 tests/fixtures/text-reading-order.pdf
cp "$tmp/out" "$tmp/expected"
mkdir "$tmp/dir"
run o-success 0 -o "$tmp/dir/out.txt" tests/fixtures/text-reading-order.pdf
[ ! -s "$tmp/out" ] || fail "o-success: stdout must be empty"
cmp -s "$tmp/expected" "$tmp/dir/out.txt" || fail "o-success: file differs from stdout text"
[ "$(ls "$tmp/dir")" = "out.txt" ] || fail "o-success: temporary file left behind"
run o-before-file 0 tests/fixtures/text-reading-order.pdf -o "$tmp/dir/out2.txt"
cmp -s "$tmp/expected" "$tmp/dir/out2.txt" || fail "o-before-file: option order"
rm "$tmp/dir/out2.txt"

# Failure keeps an existing file untouched and leaves no temp file.
printf 'keep me\n' > "$tmp/dir/out.txt"
run o-failure 4 -o "$tmp/dir/out.txt" tests/fixtures/font-later-failure.pdf
[ "$(cat "$tmp/dir/out.txt")" = "keep me" ] || fail "o-failure: existing file modified"
[ "$(ls "$tmp/dir")" = "out.txt" ] || fail "o-failure: temporary file left behind"
run o-no-dir 2 -o "$tmp/missing/out.txt" tests/fixtures/font-pages.pdf
grep -q 'output: io error' "$tmp/err" || fail "o-no-dir: io error message"

# No text: empty file (created), message on stderr, exit 0.
run o-no-text 0 -o "$tmp/dir/empty.txt" tests/fixtures/pages-multi.pdf
[ -f "$tmp/dir/empty.txt" ] && [ ! -s "$tmp/dir/empty.txt" ] || fail "o-no-text: empty file expected"
grep -qx 'pdftext: no extractable text layer' "$tmp/err" || fail "o-no-text: message"

# Long basenames still work (the temp file uses a short hidden name).
long=$(printf 'a%.0s' $(seq 1 250))
run o-long-name 0 -o "$tmp/dir/$long" tests/fixtures/font-pages.pdf
[ -s "$tmp/dir/$long" ] || fail "o-long-name: file missing"
rm -f "$tmp/dir/$long"
usage_case o-dash-value -o --dump-pages tests/hello.pdf
[ ! -e ./--dump-pages ] || fail "o-dash-value: created a file named --dump-pages"
usage_case o-after-double-dash -- -o "$tmp/x.txt" tests/fixtures/font-pages.pdf
# -o onto a directory fails with io and leaves no temp file.
mkdir "$tmp/dir/sub"
run o-directory 2 -o "$tmp/dir/sub" tests/fixtures/font-pages.pdf
[ -d "$tmp/dir/sub" ] && [ -z "$(ls -A "$tmp/dir/sub")" ] || fail "o-directory: target changed"
# U+FFFD warning goes to stderr; the file holds exactly the text.
run o-fffd 0 -o "$tmp/dir/w.txt" tests/fixtures/font-winansi.pdf
cmp -s tests/golden/text-winansi.stdout "$tmp/dir/w.txt" || fail "o-fffd: file content"
cmp -s tests/golden/text-winansi.stderr "$tmp/err" || fail "o-fffd: stderr warning"
rm -f "$tmp/dir/w.txt"
[ "$(ls -A "$tmp/dir" | grep -c pdftext-)" -eq 0 ] || fail "temporary files left behind"
# A full stdout is an I/O error in every mode.
if [ -w /dev/full ]; then
    for args in "tests/fixtures/font-pages.pdf" "--dump-pages tests/hello.pdf" "--dump-text-items tests/fixtures/font-pages.pdf"; do
        status=0
        # shellcheck disable=SC2086
        "$binary" $args > /dev/full 2> "$tmp/err" || status=$?
        [ "$status" -eq 2 ] || fail "dev-full ($args): expected exit 2, got $status"
    done
fi

# A directory as input is an I/O error, not a resource limit.
run input-directory 2 "$tmp/dir"
grep -q 'not a regular file' "$tmp/err" || fail "input-directory: message"

# "--" ends options.
run double-dash 0 -- tests/fixtures/font-pages.pdf
[ -s "$tmp/out" ] || fail "double-dash: text expected"

# Header, trailer and object dumps.
run header 0 --dump-header tests/fixtures/geometry-raw.pdf
[ "$(cat "$tmp/out")" = "HEADER PDF-1.4" ] || fail "header output"
run header-bad 3 --dump-header tests/numbers.txt
[ ! -s "$tmp/out" ] || fail "header-bad: stdout must be empty"
run trailer 0 --dump-trailer tests/fixtures/geometry-raw.pdf
cmp -s tests/golden/cli-trailer.stdout "$tmp/out" || fail "trailer output"
run object 0 --dump-object 5 tests/fixtures/geometry-raw.pdf
cmp -s tests/golden/cli-object5.stdout "$tmp/out" || fail "object output"
run object-stream 0 --dump-object 4 tests/fixtures/geometry-raw.pdf
tail -n 1 "$tmp/out" | grep -qx 'STREAM 365 bytes' || fail "object stream length"
run object-free 3 --dump-object 99 tests/fixtures/geometry-raw.pdf
[ ! -s "$tmp/out" ] || fail "object-free: stdout must be empty"

if [ "$failures" -ne 0 ]; then
    printf 'cli tests: %d failure(s)\n' "$failures" >&2
    exit 1
fi
printf '%s\n' 'cli tests: usage, help/version, -o atomic output and dump modes passed'

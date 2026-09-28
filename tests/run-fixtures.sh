#!/bin/sh

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd) || exit 1
binary=${1:-"$project_root/pdftext"}
manifest="$project_root/tests/fixtures.tsv"

if [ ! -x "$binary" ]; then
	printf 'fixture runner: binary is not executable: %s\n' "$binary" >&2
	exit 1
fi

if [ ! -r "$manifest" ]; then
	printf 'fixture runner: manifest is not readable: %s\n' "$manifest" >&2
	exit 1
fi

work_dir=${TMPDIR:-/tmp}/pdftext-fixtures.$$
if ! (umask 077 && mkdir "$work_dir"); then
	printf 'fixture runner: could not create temporary directory: %s\n' "$work_dir" >&2
	exit 1
fi

cleanup() {
	cleanup_status=$?
	rm -f "$work_dir"/*.stdout "$work_dir"/*.stderr
	rmdir "$work_dir"
	return "$cleanup_status"
}

trap cleanup 0
trap 'exit 1' 1 2 3 15

failures=0
total=0

relative_path() {
	case "$1" in
		/*) printf '%s\n' "$1" ;;
		*) printf '%s/%s\n' "$project_root" "$1" ;;
	esac
}

compare_output() {
	fixture_name=$1
	stream_name=$2
	expected_file=$3
	actual_file=$4

	if [ "$expected_file" = "-" ]; then
		if [ -s "$actual_file" ]; then
			printf 'fixture %s: %s expected to be empty\n' \
				"$fixture_name" "$stream_name" >&2
			diff -u /dev/null "$actual_file" >&2 || :
			return 1
		fi
		return 0
	fi

	expected_path=$(relative_path "$expected_file")
	if [ ! -f "$expected_path" ]; then
		printf 'fixture %s: missing %s golden file: %s\n' \
			"$fixture_name" "$stream_name" "$expected_path" >&2
		return 1
	fi

	if cmp -s "$expected_path" "$actual_file"; then
		return 0
	fi

	printf 'fixture %s: %s differs\n' "$fixture_name" "$stream_name" >&2
	diff -u "$expected_path" "$actual_file" >&2 || :
	return 1
}

while IFS='|' read -r fixture_name result_kind input expected_status expected_stdout expected_stderr
do
	case "$fixture_name" in
		''|'#'*) continue ;;
	esac

	total=$((total + 1))

	case "$result_kind" in
		success|malformed|unsupported|io|resource-limit) ;;
		*)
			printf '[FAIL] %s: unknown result kind: %s\n' \
				"$fixture_name" "$result_kind" >&2
			failures=$((failures + 1))
			continue
			;;
	esac

	case "$expected_status" in
		''|*[!0-9]*)
			printf '[FAIL] %s: invalid expected exit code: %s\n' \
				"$fixture_name" "$expected_status" >&2
			failures=$((failures + 1))
			continue
			;;
	esac

	fixture_mode=stream
	case "$input" in
		object:*)
			fixture_mode=object
			input=${input#object:}
			;;
		indirect:*)
			fixture_mode=indirect
			input=${input#indirect:}
			;;
		xref:*)
			fixture_mode=xref
			input=${input#xref:}
			;;
		pages:*)
			fixture_mode=pages
			input=${input#pages:}
			;;
		content:*)
			fixture_mode=content
			input=${input#content:}
			;;
		items:*)
			fixture_mode=items
			input=${input#items:}
			;;
		contents:*)
			fixture_mode=contents
			input=${input#contents:}
			;;
	esac
	fixture_path=$(relative_path "$input")
	actual_stdout="$work_dir/$total.stdout"
	actual_stderr="$work_dir/$total.stderr"

	if [ "$fixture_mode" = object ]; then
		if "$binary" --object "$fixture_path" >"$actual_stdout" 2>"$actual_stderr"; then
			actual_status=0
		else
			actual_status=$?
		fi
	elif [ "$fixture_mode" = indirect ]; then
		if "$binary" --indirect "$fixture_path" >"$actual_stdout" 2>"$actual_stderr"; then
			actual_status=0
		else
			actual_status=$?
		fi
	elif [ "$fixture_mode" = xref ]; then
		if "$binary" --dump-xref "$fixture_path" >"$actual_stdout" 2>"$actual_stderr"; then
			actual_status=0
		else
			actual_status=$?
		fi
	elif [ "$fixture_mode" = pages ]; then
		if "$binary" --dump-pages "$fixture_path" >"$actual_stdout" 2>"$actual_stderr"; then
			actual_status=0
		else
			actual_status=$?
		fi
	elif [ "$fixture_mode" = content ]; then
		if "$binary" --dump-content "$fixture_path" >"$actual_stdout" 2>"$actual_stderr"; then
			actual_status=0
		else
			actual_status=$?
		fi
	elif [ "$fixture_mode" = items ]; then
		if "$binary" --dump-text-items "$fixture_path" >"$actual_stdout" 2>"$actual_stderr"; then
			actual_status=0
		else
			actual_status=$?
		fi
	elif [ "$fixture_mode" = contents ]; then
		if "$binary" --dump-contents "$fixture_path" >"$actual_stdout" 2>"$actual_stderr"; then
			actual_status=0
		else
			actual_status=$?
		fi
	elif "$binary" "$fixture_path" >"$actual_stdout" 2>"$actual_stderr"; then
		actual_status=0
	else
		actual_status=$?
	fi

	fixture_failed=0
	if [ "$actual_status" -ne "$expected_status" ]; then
		printf 'fixture %s (%s): expected exit %s, got %s\n' \
			"$fixture_name" "$result_kind" "$expected_status" "$actual_status" >&2
		fixture_failed=1
	fi

	if ! compare_output "$fixture_name" stdout "$expected_stdout" "$actual_stdout"; then
		fixture_failed=1
	fi

	if ! compare_output "$fixture_name" stderr "$expected_stderr" "$actual_stderr"; then
		fixture_failed=1
	fi

	if [ "$fixture_failed" -eq 0 ]; then
		printf '[PASS] %s (%s)\n' "$fixture_name" "$result_kind"
	else
		failures=$((failures + 1))
	fi
done < "$manifest"

if [ "$failures" -eq 0 ]; then
	printf 'fixture runner: %s fixtures passed\n' "$total"
	exit 0
fi

printf 'fixture runner: %s passed, %s failed\n' \
	$((total - failures)) "$failures" >&2
exit 1

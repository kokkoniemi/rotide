#!/bin/sh
set -eu

table=${1:-tools/module-prefixes.tsv}
src_dir=${2:-src}

if [ ! -f "$table" ]; then
	echo "lint-prefixes: $table not found; prefix checks are advisory until phase 5"
	exit 0
fi

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

find "$src_dir" -type f -name '*.c' | sort > "$tmp"

status=0
while IFS= read -r file; do
	if ! awk -F '	' -v file="$file" '
		$0 ~ /^[[:space:]]*(#|$)/ { next }
		$1 == file { found = 1 }
		END { exit found ? 0 : 1 }
	' "$table"; then
		echo "lint-prefixes: missing prefix entry for $file" >&2
		status=1
	fi
done < "$tmp"

if [ "$status" -ne 0 ]; then
	exit "$status"
fi

echo "lint-prefixes: table present; static-name checks are advisory until phase 6"

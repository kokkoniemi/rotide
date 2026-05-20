#!/bin/sh
set -eu

table=${1:-tools/module-prefixes.tsv}
src_dir=${2:-src}

if [ ! -f "$table" ]; then
	echo "lint-prefixes: $table not found" >&2
	exit 1
fi

src_files=$(mktemp)
table_rows=$(mktemp)
table_files=$(mktemp)
missing=$(mktemp)
extra=$(mktemp)
warnings=$(mktemp)
trap 'rm -f "$src_files" "$table_rows" "$table_files" "$missing" "$extra" "$warnings"' EXIT

find "$src_dir" -type f -name '*.c' | sort > "$src_files"

awk -F '	' '
	$0 ~ /^[[:space:]]*(#|$)/ { next }
	NF < 2 || $1 == "" || $2 == "" {
		printf "lint-prefixes: malformed table row %d\n", NR > "/dev/stderr"
		status = 1
		next
	}
	seen[$1]++ {
		printf "lint-prefixes: duplicate prefix entry for %s\n", $1 > "/dev/stderr"
		status = 1
		next
	}
	{ print $1 "\t" $2 }
	END { exit status }
' "$table" > "$table_rows"

cut -f 1 "$table_rows" | sort > "$table_files"
comm -23 "$src_files" "$table_files" > "$missing"
comm -13 "$src_files" "$table_files" > "$extra"

status=0
if [ -s "$missing" ]; then
	sed 's/^/lint-prefixes: missing prefix entry for /' "$missing" >&2
	status=1
fi
if [ -s "$extra" ]; then
	sed 's/^/lint-prefixes: stale prefix entry for /' "$extra" >&2
	status=1
fi
if [ "$status" -ne 0 ]; then
	exit "$status"
fi

while IFS="$(printf '\t')" read -r file prefixes; do
	awk -v file="$file" -v prefixes="$prefixes" '
		BEGIN {
			prefix_count = split(prefixes, raw_prefixes, ",")
		}

		function trim(text) {
			sub(/^[ \t]+/, "", text)
			sub(/[ \t]+$/, "", text)
			return text
		}

		function prefix_ok(name, i, prefix) {
			for (i = 1; i <= prefix_count; i++) {
				prefix = trim(raw_prefixes[i])
				if (prefix != "" && index(name, prefix) == 1) {
					return 1
				}
			}
			return 0
		}

		function check_signature(signature, line_no, text, parts, part_count, name) {
			if (signature !~ /\)[ \t]*\{/) {
				return
			}
			text = signature
			sub(/\(.*/, "", text)
			gsub(/\*/, " ", text)
			gsub(/[ \t]+/, " ", text)
			text = trim(text)
			part_count = split(text, parts, " ")
			name = parts[part_count]
			if (name == "" || name == "static") {
				return
			}
			if (!prefix_ok(name)) {
				printf "%s:%d: %s does not use prefix %s\n", file, line_no, name, prefixes
			}
		}

		/^[ \t]*static[ \t].*\(/ {
			signature = $0
			start_line = FNR
			collecting = 1
			if ($0 ~ /\)[ \t]*\{/) {
				check_signature(signature, start_line)
				collecting = 0
			}
			next
		}

		collecting {
			signature = signature " " $0
			if ($0 ~ /\)[ \t]*\{/) {
				check_signature(signature, start_line)
				collecting = 0
			}
		}
	' "$file" >> "$warnings"
done < "$table_rows"

warning_count=$(wc -l < "$warnings" | tr -d ' ')
if [ "$warning_count" -ne 0 ]; then
	echo "lint-prefixes: static-name advisory: $warning_count mismatch(es)" >&2
	head -n 20 "$warnings" >&2
	if [ "$warning_count" -gt 20 ]; then
		echo "lint-prefixes: showing first 20 mismatches; set LINT_PREFIXES_STRICT=1 to fail on all" >&2
	fi
	if [ "${LINT_PREFIXES_STRICT:-0}" = "1" ]; then
		exit 1
	fi
fi

source_count=$(wc -l < "$src_files" | tr -d ' ')
echo "lint-prefixes: table complete for $source_count source files; static-name checks are advisory"

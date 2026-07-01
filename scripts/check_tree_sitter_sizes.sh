#!/bin/sh

set -eu

cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

baseline=${1:-vendor/tree_sitter/SIZES.tsv}
if [ ! -f "$baseline" ]; then
	echo "tree-sitter sizes: missing baseline: $baseline" >&2
	exit 1
fi

current=$(mktemp)
trap 'rm -f "$current"' EXIT
scripts/report_tree_sitter_sizes.sh > "$current"

awk -F '\t' '
	BEGIN {
		max_percent = 5
		max_bytes = 262144
	}
	FNR == 1 {
		expected = "language\tparser_lines\tparser_bytes\tscanner_lines\t" \
		           "scanner_bytes\tscanner_support_bytes\tstate_count\t" \
		           "large_state_count\tsymbol_count\ttoken_count\t" \
		           "external_token_count\tparser_object_bytes"
		if ($0 != expected) {
			printf "tree-sitter sizes: invalid header in %s\n", FILENAME > "/dev/stderr"
			exit 2
		}
		next
	}
	FILENAME == ARGV[1] {
		baseline_bytes[$1] = $3
		baseline_seen[$1] = 1
		next
	}
	{
		current_seen[$1] = 1
		if (!baseline_seen[$1]) {
			printf "tree-sitter sizes: %s has no baseline\n", $1 > "/dev/stderr"
			failed = 1
			next
		}
		byte_growth = $3 - baseline_bytes[$1]
		percent_growth = baseline_bytes[$1] == 0 ? 100 : \
		                 (100.0 * byte_growth / baseline_bytes[$1])
		if (byte_growth > max_bytes && percent_growth > max_percent) {
			printf "tree-sitter sizes: %s parser.c grew by %d bytes (%.1f%%)\n", \
			       $1, byte_growth, percent_growth > "/dev/stderr"
			failed = 1
		}
	}
	END {
		for (language in baseline_seen) {
			if (!current_seen[language]) {
				printf "tree-sitter sizes: baseline language %s is missing\n", \
				       language > "/dev/stderr"
				failed = 1
			}
		}
		if (failed) {
			print "tree-sitter sizes: update the grammar or baseline intentionally" \
			      > "/dev/stderr"
			exit 1
		}
	}
' "$baseline" "$current"

echo "OK: Tree-sitter parser sizes are within the growth limit"

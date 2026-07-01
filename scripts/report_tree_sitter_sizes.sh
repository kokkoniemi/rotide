#!/bin/sh

set -eu

cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

object_root=""
if [ "$#" -gt 0 ]; then
	if [ "$#" -ne 2 ] || [ "$1" != "--object-root" ]; then
		echo "usage: $0 [--object-root build-dir]" >&2
		exit 2
	fi
	object_root=$2
fi

grammar_root=vendor/tree_sitter/grammars

file_lines() {
	wc -l < "$1" | tr -d ' '
}

file_bytes() {
	wc -c < "$1" | tr -d ' '
}

parser_define() {
	awk -v name="$2" '$1 == "#define" && $2 == name { print $3; found = 1; exit }
		END { if (!found) exit 1 }' "$1"
}

scanner_support_bytes() {
	find "$1" -type f \( -name scanner.h -o -name unicode.h \) -exec wc -c {} + \
		| awk '$2 != "total" { total += $1 } END { print total + 0 }'
}

printf '%s\n' \
	'language	parser_lines	parser_bytes	scanner_lines	scanner_bytes	scanner_support_bytes	state_count	large_state_count	symbol_count	token_count	external_token_count	parser_object_bytes'

find "$grammar_root" -mindepth 1 -maxdepth 1 -type d -print | LC_ALL=C sort |
while IFS= read -r grammar_dir; do
	language=${grammar_dir##*/}
	parser=$grammar_dir/src/parser.c
	[ -f "$parser" ] || continue

	parser_lines=$(file_lines "$parser")
	parser_bytes=$(file_bytes "$parser")
	scanner=$grammar_dir/src/scanner.c
	scanner_lines=0
	scanner_bytes=0
	if [ -f "$scanner" ]; then
		scanner_lines=$(file_lines "$scanner")
		scanner_bytes=$(file_bytes "$scanner")
	fi

	object_bytes=0
	if [ -n "$object_root" ]; then
		object=$object_root/vendor/tree_sitter/grammars/$language/src/parser.o
		if [ -f "$object" ]; then
			object_bytes=$(file_bytes "$object")
		fi
	fi

	printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
		"$language" "$parser_lines" "$parser_bytes" "$scanner_lines" \
		"$scanner_bytes" "$(scanner_support_bytes "$grammar_dir")" \
		"$(parser_define "$parser" STATE_COUNT)" \
		"$(parser_define "$parser" LARGE_STATE_COUNT)" \
		"$(parser_define "$parser" SYMBOL_COUNT)" \
		"$(parser_define "$parser" TOKEN_COUNT)" \
		"$(parser_define "$parser" EXTERNAL_TOKEN_COUNT)" "$object_bytes"
done

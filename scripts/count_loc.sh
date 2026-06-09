#!/bin/sh
# Emit lines-of-code metrics rows (kind="loc"), one per scope/domain, for the
# SVG trend dashboard. Mirrors the bench/fuzz emitters: writes JSONL to the
# path in METRICS_OUT (appending) or to stdout, and enriches each row from the
# ROTIDE_METRICS_GIT_SHA / GIT_REF / CI_RUN_ID env vars when present.
#
# Domains are derived from the tracked source tree so new src/ subsystems show
# up automatically:
#   scope=first_party : one domain per src/<subdir>, plus "core" for src/*.{c,h}
#   scope=tests       : the tests/ tree (domain "tests")
#   scope=vendor      : each vendored library (kept on its own chart/scale)
# Only git-tracked .c/.h/.cc/.cpp/.hpp files are counted, so generated blobs
# (e.g. src/rotide.h.gch) never inflate the totals.
#
# Per-domain churn (lines added + deleted) since the previous sample is also
# emitted, so activity stays visible even when code_lines nets flat (e.g. an
# equal create+delete within one domain). The churn range is:
#   $ROTIDE_LOC_BASE_SHA..HEAD   when ROTIDE_LOC_BASE_SHA is set (CI passes the
#                                push's "before" SHA), else HEAD~1..HEAD.
# When a base resolves and no tracked source changed in that range, the script
# emits nothing (no redundant flat point) unless ROTIDE_LOC_FORCE=1. The first
# sample (no resolvable base) always emits, with zero churn.
set -eu

cd "$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

src_exts_re='\.(c|h|cc|cpp|hpp)$'
out="${METRICS_OUT:-}"
ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
git_sha="${ROTIDE_METRICS_GIT_SHA:-}"
git_ref="${ROTIDE_METRICS_GIT_REF:-}"
ci_run_id="${ROTIDE_METRICS_CI_RUN_ID:-}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# All tracked source files under the trees we measure.
git ls-files -- 'src/*.c' 'src/*.h' 'src/*.cc' 'src/*.cpp' 'src/*.hpp' \
	'tests/*.c' 'tests/*.h' 'tests/*.cc' 'tests/*.cpp' 'tests/*.hpp' \
	'vendor/*.c' 'vendor/*.h' 'vendor/*.cc' 'vendor/*.cpp' 'vendor/*.hpp' \
	> "$work/files"

# Classify a path to "scope domain", or print nothing if it falls outside the
# measured trees. Used for both file lists and churn paths.
classify() {
	awk '{
		p=$0
		n=split(p, a, "/")
		if (a[1]=="src") {
			if (n>=3) print "first_party " a[2]
			else if (n==2) print "first_party core"
		} else if (a[1]=="tests") {
			print "tests tests"
		} else if (a[1]=="vendor" && n>=2) {
			print "vendor " a[2]
		}
	}' "$1"
}

# Distinct scope/domain buckets present in the tree, sorted for stable output.
classify "$work/files" | sort -u > "$work/buckets"

# --- Source-changed dedup -------------------------------------------------
base="${ROTIDE_LOC_BASE_SHA:-}"
if [ -z "$base" ] && git rev-parse --verify -q HEAD~1 >/dev/null 2>&1; then
	base="HEAD~1"
fi
if [ -n "$base" ] && ! git rev-parse --verify -q "$base" >/dev/null 2>&1; then
	base=""  # unresolvable (e.g. shallow clone, first commit) → treat as no base
fi

if [ -n "$base" ] && [ "${ROTIDE_LOC_FORCE:-0}" != "1" ]; then
	if ! git diff --name-only "$base"..HEAD -- src tests vendor \
		| grep -Eq "$src_exts_re"; then
		echo "count_loc: no tracked source changed since $base; nothing to emit" >&2
		exit 0
	fi
fi

# --- Per-domain churn -----------------------------------------------------
# numstat: "added<TAB>deleted<TAB>path"; binary files report "-". Aggregate
# added+deleted per bucket into $work/churn as "scope/domain added deleted".
: > "$work/churn"
if [ -n "$base" ]; then
	git diff --numstat "$base"..HEAD -- src tests vendor \
		| grep -E "	[^	]*$src_exts_re$" > "$work/numstat" || true
	awk -F'\t' '{
		added = ($1=="-") ? 0 : $1
		deleted = ($2=="-") ? 0 : $2
		p=$3
		n=split(p, a, "/")
		key=""
		if (a[1]=="src") { key=(n>=3)?("first_party/" a[2]):"first_party/core" }
		else if (a[1]=="tests") { key="tests/tests" }
		else if (a[1]=="vendor" && n>=2) { key="vendor/" a[2] }
		if (key!="") { add[key]+=added; del[key]+=deleted }
	} END {
		for (k in add) print k, add[k], del[k]
	}' "$work/numstat" > "$work/churn"
fi

churn_lookup() {  # $1=scope $2=domain → "added deleted" (0 0 if none)
	awk -v k="$1/$2" '$1==k { print $2, $3; found=1 } END { if (!found) print 0, 0 }' \
		"$work/churn"
}

have_cloc=0
if command -v cloc >/dev/null 2>&1; then
	have_cloc=1
fi

# Count code/comment/blank/files for a bucket's file list ($1). Prints
# "code comment blank files". Uses cloc when available for the comment/blank
# split, else falls back to wc (all non-empty content counted as code).
count_bucket() {
	list="$1"
	if [ ! -s "$list" ]; then
		echo "0 0 0 0"
		return
	fi
	files=$(wc -l < "$list" | tr -d ' ')
	if [ "$have_cloc" -eq 1 ]; then
		# cloc --csv emits a trailing "SUM,files,blank,comment,code" line.
		cloc --quiet --csv --list-file="$list" 2>/dev/null \
			| awk -F, '/^SUM,/ { print $5, $4, $3, $2; found=1 }
			           END { if (!found) print 0, 0, 0, 0 }'
		return
	fi
	# No comment/blank split without cloc: count every line as code. Summing
	# via `cat | wc -l` avoids wc's per-file/total ambiguity.
	code=$(xargs cat < "$list" 2>/dev/null | wc -l | tr -d ' ')
	echo "$code 0 0 $files"
}

emit() {  # writes one JSONL row to stdout
	scope="$1"; domain="$2"; code="$3"; comment="$4"; blank="$5"; files="$6"
	added="$7"; deleted="$8"
	meta=""
	[ -n "$git_sha" ] && meta="$meta,\"git_sha\":\"$git_sha\""
	[ -n "$git_ref" ] && meta="$meta,\"git_ref\":\"$git_ref\""
	[ -n "$ci_run_id" ] && meta="$meta,\"ci_run_id\":\"$ci_run_id\""
	printf '{"kind":"loc","ts":"%s"%s,"scope":"%s","domain":"%s","code_lines":%s,"comment_lines":%s,"blank_lines":%s,"files":%s,"lines_added":%s,"lines_deleted":%s}\n' \
		"$ts" "$meta" "$scope" "$domain" "$code" "$comment" "$blank" "$files" "$added" "$deleted"
}

{
	while read -r scope domain; do
		[ -n "$scope" ] || continue
		case "$scope/$domain" in
			first_party/core)
				awk -F/ 'NF==2 && $1=="src"' "$work/files" > "$work/list" ;;
			first_party/*)
				grep -E "^src/$domain/" "$work/files" > "$work/list" || true ;;
			tests/tests)
				grep -E '^tests/' "$work/files" > "$work/list" || true ;;
			vendor/*)
				grep -E "^vendor/$domain/" "$work/files" > "$work/list" || true ;;
			*) : > "$work/list" ;;
		esac
		set -- $(count_bucket "$work/list")
		code="$1"; comment="$2"; blank="$3"; files="$4"
		set -- $(churn_lookup "$scope" "$domain")
		added="$1"; deleted="$2"
		emit "$scope" "$domain" "$code" "$comment" "$blank" "$files" "$added" "$deleted"
	done < "$work/buckets"
} > "$work/rows"

if [ -n "$out" ]; then
	cat "$work/rows" >> "$out"
	echo "count_loc: appended $(wc -l < "$work/rows" | tr -d ' ') row(s) to $out" >&2
else
	cat "$work/rows"
fi

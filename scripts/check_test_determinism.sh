#!/bin/sh
# Determinism gate. Runs the binary twice with a fixed seed and diffs the
# outcome lines (PASS/FAIL/SKIP/RESET-DRIFT/summary).
#
# Usage: check_test_determinism.sh <path-to-rotide_tests> [extra-flags...]
#
# On failure, the raw and filtered outputs from both runs are copied to
# tests/artifacts/determinism/ so CI can upload them for inspection.

set -eu

bin=${1:?usage: $0 <rotide_tests> [extra-flags...]}
shift
seed=${ROTIDE_DETERMINISM_SEED:-0xC0FFEE0DDBA11}

artifact_dir="tests/artifacts/determinism"

tmpdir=$(mktemp -d -t rotide-determinism.XXXXXX)
trap 'rm -rf "$tmpdir"' EXIT

# Filter to outcome lines: test stdout has nondeterministic content
# (mkstemp suffixes in captured screens, etc.) that isn't a real failure.
filter() {
	grep -E '^(PASS|FAIL|SKIP|RESET-DRIFT|[0-9]+/[0-9]+ test runs )'
}

# Preserve raw and filtered outputs so a CI upload step can grab them.
save_artifacts() {
	mkdir -p "$artifact_dir"
	for f in run1.raw run2.raw run1.out run2.out; do
		if [ -f "$tmpdir/$f" ]; then
			cp "$tmpdir/$f" "$artifact_dir/$f"
		fi
	done
	if [ -f "$tmpdir/run1.out" ] && [ -f "$tmpdir/run2.out" ]; then
		diff -u "$tmpdir/run1.out" "$tmpdir/run2.out" \
			>"$artifact_dir/outcome.diff" 2>/dev/null || true
	fi
}

run1="$tmpdir/run1.out"
run2="$tmpdir/run2.out"

raw1="$tmpdir/run1.raw"
raw2="$tmpdir/run2.raw"

# Use `|| rc=$?` so a nonzero exit from the binary doesn't abort the
# script via `set -e` before we can compare the two runs.
rc1=0
"$bin" --seed "$seed" "$@" >"$raw1" 2>&1 || rc1=$?
rc2=0
"$bin" --seed "$seed" "$@" >"$raw2" 2>&1 || rc2=$?

if [ "$rc1" != "$rc2" ]; then
	filter <"$raw1" >"$run1" || true
	filter <"$raw2" >"$run2" || true
	save_artifacts
	echo "FAIL: determinism gate (seed=$seed): exit codes differ: $rc1 vs $rc2" >&2
	exit 1
fi

filter <"$raw1" >"$run1"
filter <"$raw2" >"$run2"

if [ ! -s "$run1" ]; then
	save_artifacts
	echo "FAIL: determinism gate (seed=$seed): no outcome lines from $bin (exit $rc1)" >&2
	tail -20 "$raw1" >&2
	exit 1
fi

if ! diff -q "$run1" "$run2" >/dev/null; then
	save_artifacts
	echo "FAIL: determinism gate (seed=$seed)" >&2
	diff -u "$run1" "$run2" | head -200 >&2
	exit 1
fi

echo "OK: determinism gate (seed=$seed, $(wc -l <"$run1") outcome lines, exit $rc1)"

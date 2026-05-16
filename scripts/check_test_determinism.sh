#!/bin/sh
# Determinism gate. Runs the test binary twice with a fixed seed and
# verifies byte-identical output (PASS/FAIL lines, drift report, summary).
# Any nondeterminism in tests, seeded RNGs, or runner output fails the gate
# *before* it costs an engineer half a day chasing an unreproducible bug.
#
# Usage: check_test_determinism.sh <path-to-rotide_tests> [extra-flags...]

set -eu

bin=${1:?usage: $0 <rotide_tests> [extra-flags...]}
shift
seed=${ROTIDE_DETERMINISM_SEED:-0xC0FFEE0DDBA11}

tmpdir=$(mktemp -d -t rotide-determinism.XXXXXX)
trap 'rm -rf "$tmpdir"' EXIT

# Test stdout legitimately contains nondeterministic data (mkstemp suffixes
# in captured-screen output, /dev/urandom-seeded RNG when no seed is forced
# upstream, etc.). The gate cares about test *outcomes* and validator
# reports, not incidental log content. Filter to those lines.
filter() {
	grep -E '^(PASS|FAIL|SKIP|RESET-DRIFT|[0-9]+/[0-9]+ test runs )'
}

run1="$tmpdir/run1.out"
run2="$tmpdir/run2.out"

raw1="$tmpdir/run1.raw"
raw2="$tmpdir/run2.raw"

"$bin" --seed "$seed" "$@" >"$raw1" 2>&1
rc1=$?
"$bin" --seed "$seed" "$@" >"$raw2" 2>&1
rc2=$?

if [ "$rc1" != "$rc2" ]; then
	echo "FAIL: determinism gate (seed=$seed): exit codes differ: $rc1 vs $rc2" >&2
	exit 1
fi

filter <"$raw1" >"$run1"
filter <"$raw2" >"$run2"

if [ ! -s "$run1" ]; then
	echo "FAIL: determinism gate (seed=$seed): no outcome lines from $bin (exit $rc1)" >&2
	tail -20 "$raw1" >&2
	exit 1
fi

if ! diff -q "$run1" "$run2" >/dev/null; then
	echo "FAIL: determinism gate (seed=$seed)" >&2
	diff -u "$run1" "$run2" | head -200 >&2
	exit 1
fi

echo "OK: determinism gate (seed=$seed, $(wc -l <"$run1") outcome lines, exit $rc1)"

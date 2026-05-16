#!/bin/sh
# Smoke-test the crash handler: inject a SIGSEGV via ROTIDE_TEST_CRASH and
# verify CRASH line, artifact file fields, and exit status 1.
#
# Usage: check_crash_handler.sh <path-to-rotide_tests>

set -eu

bin=${1:?usage: $0 <rotide_tests>}
target_suite=document_text_editing
target_test=utf8_decode_valid_sequences
artifact="tests/artifacts/crashes/$target_suite/$target_test.crash"
log=$(mktemp -t rotide-crash-test.XXXXXX)
trap 'rm -f "$log"' EXIT

rm -f "$artifact"

# Non-zero exit is success here.
set +e
ROTIDE_TEST_CRASH="$target_suite/$target_test" \
	"$bin" --jobs 2 --filter "$target_test" --seed 0x1 \
	>"$log" 2>&1
rc=$?
set -e

if [ "$rc" -eq 0 ]; then
	echo "FAIL: expected non-zero exit, got 0" >&2
	cat "$log" >&2
	exit 1
fi

if ! grep -q "^CRASH suite=$target_suite test=$target_test signal=11" "$log"; then
	echo "FAIL: expected CRASH line not found in output" >&2
	cat "$log" >&2
	exit 1
fi

if [ ! -f "$artifact" ]; then
	echo "FAIL: artifact missing at $artifact" >&2
	exit 1
fi

for field in "signal=11" "suite=$target_suite" "test=$target_test" "seed=0x" "backtrace:"; do
	if ! grep -q "^$field" "$artifact"; then
		echo "FAIL: artifact missing field '$field'" >&2
		cat "$artifact" >&2
		exit 1
	fi
done

echo "OK: crash handler (artifact=$artifact, exit=$rc)"

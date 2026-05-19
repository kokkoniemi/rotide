#!/bin/sh
# Quarantine pass-now check. Runs the test binary with --no-quarantine and
# fails if any test currently listed in tests/QUARANTINE.md emits a PASS
# line. A quarantined test that starts passing again is the signal to
# delete the entry, not ignore it.
#
# Usage: check_quarantine_passing.sh <path-to-rotide_tests> [extra-flags...]

set -eu

bin=${1:?usage: $0 <rotide_tests> [extra-flags...]}
shift
qfile=${ROTIDE_QUARANTINE_FILE:-tests/QUARANTINE.md}

if [ ! -f "$qfile" ]; then
	echo "OK: no quarantine file at $qfile (nothing to check)"
	exit 0
fi

# Extract `- name` entries from the active-entries section. Skip the
# prose bullets at the top of the file (they live before the "## Active
# entries" heading) and skip fenced code blocks (the format example).
names=$(awk '
	/^##[ \t]+Active entries[ \t]*$/ { in_active = 1; next }
	!in_active { next }
	/^[ \t]*```/ { in_fence = !in_fence; next }
	in_fence { next }
	/^[ \t]*-[ \t]+/ {
		line = $0
		sub(/^[ \t]*-[ \t]+/, "", line)
		if (match(line, /^[A-Za-z0-9_]+/) > 0) {
			print substr(line, RSTART, RLENGTH)
		}
	}
' "$qfile")

if [ -z "$names" ]; then
	echo "OK: no quarantined entries in $qfile"
	exit 0
fi

tmpdir=$(mktemp -d -t rotide-quarantine-pass.XXXXXX)
trap 'rm -rf "$tmpdir"' EXIT
log="$tmpdir/run.log"

# Allow the test binary to fail (quarantined tests typically do). We only
# care about which PASS lines appeared.
set +e
"$bin" --no-quarantine "$@" >"$log" 2>&1
set -e

fail=0
echo "$names" | while IFS= read -r name; do
	[ -n "$name" ] || continue
	# PASS lines look like `PASS <name>` (optionally followed by extra
	# metadata). Anchor the name so substrings don't false-positive.
	if grep -qE "^PASS ${name}( |$)" "$log"; then
		echo "FAIL: quarantined test '$name' now PASSes — remove it from $qfile" >&2
		echo "fail" >> "$tmpdir/fail-marker"
	fi
done

if [ -e "$tmpdir/fail-marker" ]; then
	exit 1
fi

count=$(echo "$names" | grep -c .)
echo "OK: $count quarantined test(s) still failing or skipped as expected"

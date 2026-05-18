#!/bin/sh
# Quarantine age check. Fails if any tests/QUARANTINE.md entry has a
# `since:` date older than the configured window (default 30 days).
# Per-PR enforcement that keeps the quarantine list from rotting.
#
# Usage: check_quarantine_age.sh [path-to-QUARANTINE.md]
# Override the window with ROTIDE_QUARANTINE_MAX_AGE_DAYS=<n>.

set -eu

qfile=${1:-tests/QUARANTINE.md}
max_age_days=${ROTIDE_QUARANTINE_MAX_AGE_DAYS:-30}

if [ ! -f "$qfile" ]; then
	echo "OK: no quarantine file at $qfile"
	exit 0
fi

today_epoch=$(date +%s)
cutoff_epoch=$((today_epoch - max_age_days * 86400))

# Walk the file in awk. Emit "name<TAB>since" for each entry; the shell
# loop below converts each since: date to an epoch and compares.
entries=$(awk '
	function flush() {
		if (cur_name != "") {
			print cur_name "\t" cur_since
			cur_name = ""
			cur_since = ""
		}
	}
	/^##[ \t]+Active entries[ \t]*$/ { in_active = 1; next }
	!in_active { next }
	/^[ \t]*```/ { in_fence = !in_fence; next }
	in_fence { next }
	/^[ \t]*-[ \t]+/ {
		flush()
		line = $0
		sub(/^[ \t]*-[ \t]+/, "", line)
		if (match(line, /^[A-Za-z0-9_]+/) > 0) {
			cur_name = substr(line, RSTART, RLENGTH)
		}
		next
	}
	/^[ \t]+since:[ \t]+/ {
		val = $0
		sub(/^[ \t]+since:[ \t]+/, "", val)
		# Take first whitespace-delimited token.
		sub(/[ \t].*$/, "", val)
		cur_since = val
		next
	}
	END { flush() }
' "$qfile")

if [ -z "$entries" ]; then
	echo "OK: no quarantine entries"
	exit 0
fi

stale=$(mktemp)
unparsed=$(mktemp)
trap 'rm -f "$stale" "$unparsed"' EXIT

echo "$entries" | while IFS='	' read -r name since; do
	[ -n "$name" ] || continue
	if [ -z "$since" ]; then
		printf '  - %s (missing since: field)\n' "$name" >> "$unparsed"
		continue
	fi
	if ! since_epoch=$(date -d "$since" +%s 2>/dev/null); then
		printf '  - %s (since %s: unparseable date)\n' "$name" "$since" >> "$unparsed"
		continue
	fi
	if [ "$since_epoch" -lt "$cutoff_epoch" ]; then
		age_days=$(( (today_epoch - since_epoch) / 86400 ))
		printf '  - %s (since %s, age %d days)\n' "$name" "$since" "$age_days" >> "$stale"
	fi
done

if [ -s "$stale" ] || [ -s "$unparsed" ]; then
	echo "FAIL: quarantine policy violation (window: ${max_age_days} days)" >&2
	if [ -s "$stale" ]; then
		echo "Entries past the ${max_age_days}-day window:" >&2
		cat "$stale" >&2
	fi
	if [ -s "$unparsed" ]; then
		echo "Entries with missing/unparseable since: field:" >&2
		cat "$unparsed" >&2
	fi
	echo "Fix the underlying issue or re-up the entry with a fresh since: date and a comment." >&2
	exit 1
fi

count=$(echo "$entries" | grep -c .)
echo "OK: $count quarantine entry/ies within ${max_age_days}-day window"

#!/usr/bin/env bash
set -euo pipefail

export root="${HOME:-/tmp}"
function greet() {
	local name=$1
	printf '%s\n' "$name"
}

for item in alpha beta; do
	if [[ -n "$item" ]]; then
		greet "$item" | cat
	fi
done

while false; do
	echo waiting
done
until true; do
	echo ready
done
select choice in one two; do
	echo "$choice"
	break
done

if false; then
	echo no
elif true; then
	echo yes
else
	echo maybe
fi

case "$root" in
	/tmp) echo -n 'temporary' ;;
	*) echo "home: $(pwd)" ;;
esac

result="$(greet "$root")"
legacy=`date`
false && echo skipped
exec 3>output.txt
cat <<EOF
user=$USER root=${root}
EOF
diff <(printf '%s' "$result") >(cat)
unset result

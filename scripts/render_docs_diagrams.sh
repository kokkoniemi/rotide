#!/bin/sh
set -eu

src_dir="docs/diagrams/src"
out_dir="docs/diagrams/svg"

if ! command -v plantuml >/dev/null 2>&1; then
	echo "error: plantuml was not found on PATH" >&2
	echo "Install PlantUML, then rerun: make docs-diagrams" >&2
	echo "The diagram sources use PlantUML stdlib C4 includes such as <C4/C4_Container>." >&2
	exit 127
fi

mkdir -p "$out_dir"
for source in "$src_dir"/*.puml; do
	[ -e "$source" ] || continue
	base=${source##*/}
	rm -f "$out_dir/${base%.puml}.svg"
done

JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:+$JAVA_TOOL_OPTIONS }-Djava.awt.headless=true"
export JAVA_TOOL_OPTIONS
plantuml -tsvg -o "../svg" "$src_dir"/*.puml

missing=0
for source in "$src_dir"/*.puml; do
	[ -e "$source" ] || continue
	base=${source##*/}
	svg="$out_dir/${base%.puml}.svg"
	if [ ! -s "$svg" ]; then
		echo "error: PlantUML did not produce $svg from $source" >&2
		missing=1
	fi
done
if [ "$missing" -ne 0 ]; then
	exit 1
fi

for svg in "$out_dir"/*.svg; do
	[ -e "$svg" ] || continue
	perl -0pi -e 's/\r\n/\n/g; s/<!--MD5=.*?-->//gs; s/<\?plantuml.*?\?>//gs; s!(<defs/>|</defs>)!$1<rect width="100%" height="100%" fill="#FFFFFF"/>!s' "$svg"
done

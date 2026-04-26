#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VERSIONS_FILE="${REPO_ROOT}/vendor/tree_sitter/VERSIONS.env"

if [[ ! -f "${VERSIONS_FILE}" ]]; then
	echo "Missing versions file: ${VERSIONS_FILE}" >&2
	exit 1
fi

# shellcheck source=/dev/null
source "${VERSIONS_FILE}"

need_cmd() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "Missing required command: $1" >&2
		exit 1
	fi
}

need_cmd curl
need_cmd jq
need_cmd tar
need_cmd unzip
need_cmd sha256sum

case "$(uname -s)" in
	Linux)
		OS="linux"
		;;
	Darwin)
		OS="macos"
		;;
	*)
		echo "Unsupported host OS: $(uname -s)" >&2
		exit 1
		;;
esac

case "$(uname -m)" in
	x86_64|amd64)
		ARCH="x64"
		;;
	aarch64|arm64)
		ARCH="arm64"
		;;
	armv7l)
		ARCH="arm"
		;;
	i386|i686)
		ARCH="x86"
		;;
	*)
		echo "Unsupported host architecture: $(uname -m)" >&2
		exit 1
		;;
esac

CLI_ASSET="tree-sitter-cli-${OS}-${ARCH}.zip"
CLI_RELEASE_API="https://api.github.com/repos/tree-sitter/tree-sitter/releases/tags/${TREE_SITTER_CLI_RELEASE}"

TMP_DIR="$(mktemp -d)"
cleanup() {
	rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

download_cli() {
	echo "Fetching CLI release metadata: ${TREE_SITTER_CLI_RELEASE}" >&2
	local release_json="${TMP_DIR}/tree-sitter-release.json"
	curl -fsSL "${CLI_RELEASE_API}" > "${release_json}"

	local cli_url
	local cli_digest
	cli_url="$(jq -r --arg asset "${CLI_ASSET}" '.assets[] | select(.name == $asset) | .browser_download_url' "${release_json}")"
	cli_digest="$(jq -r --arg asset "${CLI_ASSET}" '.assets[] | select(.name == $asset) | .digest' "${release_json}")"

	if [[ -z "${cli_url}" || "${cli_url}" == "null" ]]; then
		echo "Could not find CLI asset '${CLI_ASSET}' in ${TREE_SITTER_CLI_RELEASE}" >&2
		exit 1
	fi
	if [[ -z "${cli_digest}" || "${cli_digest}" == "null" || "${cli_digest}" != sha256:* ]]; then
		echo "Could not read sha256 digest for '${CLI_ASSET}' from release metadata" >&2
		exit 1
	fi

	local cli_zip="${TMP_DIR}/${CLI_ASSET}"
	echo "Downloading CLI asset: ${CLI_ASSET}" >&2
	curl -fsSL "${cli_url}" -o "${cli_zip}"

	local expected_sha256="${cli_digest#sha256:}"
	local actual_sha256
	actual_sha256="$(sha256sum "${cli_zip}" | awk '{print $1}')"
	if [[ "${actual_sha256}" != "${expected_sha256}" ]]; then
		echo "Checksum mismatch for ${CLI_ASSET}" >&2
		echo "Expected: ${expected_sha256}" >&2
		echo "Actual:   ${actual_sha256}" >&2
		exit 1
	fi

	local cli_dir="${TMP_DIR}/cli"
	mkdir -p "${cli_dir}"
	unzip -q "${cli_zip}" -d "${cli_dir}"
	CLI_BIN="$(find "${cli_dir}" -type f \( -name tree-sitter -o -name tree-sitter.exe \) | head -n 1)"
	if [[ -z "${CLI_BIN}" ]]; then
		echo "Could not locate tree-sitter CLI binary after unzip" >&2
		exit 1
	fi
	chmod +x "${CLI_BIN}"
}

download_repo_tarball() {
	local repo="$1"
	local ref="$2"
	local out_var="$3"
	local stem="${repo##*/}"
	local tarball="${TMP_DIR}/${stem}.tar.gz"

	echo "Downloading ${repo} source ref: ${ref}" >&2
	curl -fsSL "https://github.com/${repo}/archive/${ref}.tar.gz" -o "${tarball}"
	local top
	top="$(tar -tzf "${tarball}" | awk -F/ 'NR == 1 { print $1; exit }')"
	tar -xzf "${tarball}" -C "${TMP_DIR}"

	printf -v "${out_var}" '%s' "${TMP_DIR}/${top}"
}

regenerate_parser() {
	local src_dir="$1"
	local name="$2"
	echo "Regenerating ${name} parser with official CLI binary" >&2
	(
		cd "${src_dir}"
		"${CLI_BIN}" generate
	)
}

link_grammar_dep() {
	local target_dir="$1"
	local dep_name="$2"
	local dep_src="$3"
	local modules_dir="${target_dir}/node_modules"
	mkdir -p "${modules_dir}"
	rm -rf "${modules_dir}/${dep_name}"
	ln -s "${dep_src}" "${modules_dir}/${dep_name}"
}

sync_grammar_vendor() {
	local src_dir="$1"
	local vendor_dir="$2"

	if [[ ! -d "${src_dir}/src" || ! -f "${src_dir}/grammar.js" ]]; then
		echo "Grammar source layout not found in ${src_dir}" >&2
		exit 1
	fi

	mkdir -p "${vendor_dir}"
	rm -rf "${vendor_dir}/src" "${vendor_dir}/queries"
	mkdir -p "${vendor_dir}/src"
	cp -R "${src_dir}/src/." "${vendor_dir}/src/"

	cp "${src_dir}/grammar.js" "${vendor_dir}/grammar.js"
	if [[ -f "${src_dir}/tree-sitter.json" ]]; then
		cp "${src_dir}/tree-sitter.json" "${vendor_dir}/tree-sitter.json"
	fi
	if [[ -f "${src_dir}/package.json" ]]; then
		cp "${src_dir}/package.json" "${vendor_dir}/package.json"
	fi
	if [[ -f "${src_dir}/LICENSE" ]]; then
		cp "${src_dir}/LICENSE" "${vendor_dir}/LICENSE"
	fi
	if [[ -f "${src_dir}/README.md" ]]; then
		cp "${src_dir}/README.md" "${vendor_dir}/README.upstream.md"
	fi

	if [[ -d "${src_dir}/queries" ]]; then
		mkdir -p "${vendor_dir}/queries"
		cp -R "${src_dir}/queries/." "${vendor_dir}/queries/"
	fi
}

download_cli

RUNTIME_SRC=""
C_GRAMMAR_SRC=""
CPP_GRAMMAR_SRC=""
GO_GRAMMAR_SRC=""
BASH_GRAMMAR_SRC=""
HTML_GRAMMAR_SRC=""
JAVASCRIPT_GRAMMAR_SRC=""
JSDOC_GRAMMAR_SRC=""
CSS_GRAMMAR_SRC=""
JSON_GRAMMAR_SRC=""
TYPESCRIPT_GRAMMAR_SRC=""
PYTHON_GRAMMAR_SRC=""
PHP_GRAMMAR_SRC=""
RUST_GRAMMAR_SRC=""
JAVA_GRAMMAR_SRC=""
REGEX_GRAMMAR_SRC=""
CSHARP_GRAMMAR_SRC=""
HASKELL_GRAMMAR_SRC=""
RUBY_GRAMMAR_SRC=""
OCAML_GRAMMAR_SRC=""
JULIA_GRAMMAR_SRC=""
SCALA_GRAMMAR_SRC=""
EMBEDDED_TEMPLATE_GRAMMAR_SRC=""

download_repo_tarball "tree-sitter/tree-sitter" "${TREE_SITTER_RUNTIME_REF}" RUNTIME_SRC
download_repo_tarball "tree-sitter/tree-sitter-c" "${TREE_SITTER_C_GRAMMAR_REF}" C_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-cpp" "${TREE_SITTER_CPP_GRAMMAR_REF}" CPP_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-go" "${TREE_SITTER_GO_GRAMMAR_REF}" GO_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-bash" "${TREE_SITTER_BASH_GRAMMAR_REF}" BASH_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-html" "${TREE_SITTER_HTML_GRAMMAR_REF}" HTML_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-javascript" "${TREE_SITTER_JAVASCRIPT_GRAMMAR_REF}" JAVASCRIPT_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-jsdoc" "${TREE_SITTER_JSDOC_GRAMMAR_REF}" JSDOC_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-css" "${TREE_SITTER_CSS_GRAMMAR_REF}" CSS_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-json" "${TREE_SITTER_JSON_GRAMMAR_REF}" JSON_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-typescript" "${TREE_SITTER_TYPESCRIPT_GRAMMAR_REF}" TYPESCRIPT_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-python" "${TREE_SITTER_PYTHON_GRAMMAR_REF}" PYTHON_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-php" "${TREE_SITTER_PHP_GRAMMAR_REF}" PHP_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-rust" "${TREE_SITTER_RUST_GRAMMAR_REF}" RUST_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-java" "${TREE_SITTER_JAVA_GRAMMAR_REF}" JAVA_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-regex" "${TREE_SITTER_REGEX_GRAMMAR_REF}" REGEX_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-c-sharp" "${TREE_SITTER_CSHARP_GRAMMAR_REF}" CSHARP_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-haskell" "${TREE_SITTER_HASKELL_GRAMMAR_REF}" HASKELL_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-ruby" "${TREE_SITTER_RUBY_GRAMMAR_REF}" RUBY_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-ocaml" "${TREE_SITTER_OCAML_GRAMMAR_REF}" OCAML_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-julia" "${TREE_SITTER_JULIA_GRAMMAR_REF}" JULIA_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-scala" "${TREE_SITTER_SCALA_GRAMMAR_REF}" SCALA_GRAMMAR_SRC
download_repo_tarball "tree-sitter/tree-sitter-embedded-template" "${TREE_SITTER_EMBEDDED_TEMPLATE_GRAMMAR_REF}" EMBEDDED_TEMPLATE_GRAMMAR_SRC

if [[ ! -d "${RUNTIME_SRC}/lib/src" || ! -f "${RUNTIME_SRC}/lib/include/tree_sitter/api.h" ]]; then
	echo "Runtime source layout not found in ${TREE_SITTER_RUNTIME_REF}" >&2
	exit 1
fi

regenerate_parser "${C_GRAMMAR_SRC}" "C"
# tree-sitter-cpp grammar.js does `require('tree-sitter-c/grammar')`, so expose
# the pinned C grammar source under cpp's node_modules before regenerating.
link_grammar_dep "${CPP_GRAMMAR_SRC}" "tree-sitter-c" "${C_GRAMMAR_SRC}"
regenerate_parser "${CPP_GRAMMAR_SRC}" "C++"
regenerate_parser "${GO_GRAMMAR_SRC}" "Go"
regenerate_parser "${BASH_GRAMMAR_SRC}" "Bash"
regenerate_parser "${HTML_GRAMMAR_SRC}" "HTML"
regenerate_parser "${JAVASCRIPT_GRAMMAR_SRC}" "JavaScript"
regenerate_parser "${JSDOC_GRAMMAR_SRC}" "JSDoc"
# tree-sitter-typescript grammar.js requires tree-sitter-javascript via
# common/define-grammar.js; expose the pinned JS source in node_modules.
link_grammar_dep "${TYPESCRIPT_GRAMMAR_SRC}" "tree-sitter-javascript" "${JAVASCRIPT_GRAMMAR_SRC}"
regenerate_parser "${TYPESCRIPT_GRAMMAR_SRC}/typescript" "TypeScript"
regenerate_parser "${CSS_GRAMMAR_SRC}" "CSS"
regenerate_parser "${JSON_GRAMMAR_SRC}" "JSON"
regenerate_parser "${PYTHON_GRAMMAR_SRC}" "Python"
# tree-sitter-php grammar.js requires ../common/define-grammar.js inside the
# tarball layout; regenerate from the php/ sub-grammar (HTML-mixed variant).
regenerate_parser "${PHP_GRAMMAR_SRC}/php" "PHP"
regenerate_parser "${RUST_GRAMMAR_SRC}" "Rust"
regenerate_parser "${JAVA_GRAMMAR_SRC}" "Java"
regenerate_parser "${REGEX_GRAMMAR_SRC}" "Regex"
regenerate_parser "${CSHARP_GRAMMAR_SRC}" "C#"
regenerate_parser "${HASKELL_GRAMMAR_SRC}" "Haskell"
regenerate_parser "${RUBY_GRAMMAR_SRC}" "Ruby"
# tree-sitter-ocaml ships sub-grammars under grammars/<name>/ (ocaml, interface,
# type). Only the ocaml sub-grammar is vendored; regenerate from there.
regenerate_parser "${OCAML_GRAMMAR_SRC}/grammars/ocaml" "OCaml"
regenerate_parser "${JULIA_GRAMMAR_SRC}" "Julia"
regenerate_parser "${SCALA_GRAMMAR_SRC}" "Scala"
regenerate_parser "${EMBEDDED_TEMPLATE_GRAMMAR_SRC}" "embedded-template"

RUNTIME_VENDOR="${REPO_ROOT}/vendor/tree_sitter/runtime"
mkdir -p "${RUNTIME_VENDOR}/include/tree_sitter" "${RUNTIME_VENDOR}/src"
rm -rf "${RUNTIME_VENDOR}/include/tree_sitter" "${RUNTIME_VENDOR}/src"
mkdir -p "${RUNTIME_VENDOR}/include/tree_sitter" "${RUNTIME_VENDOR}/src"
cp -R "${RUNTIME_SRC}/lib/src/." "${RUNTIME_VENDOR}/src/"
cp "${RUNTIME_SRC}/lib/include/tree_sitter/api.h" "${RUNTIME_VENDOR}/include/tree_sitter/api.h"
cp "${RUNTIME_SRC}/LICENSE" "${RUNTIME_VENDOR}/LICENSE"
cp "${RUNTIME_SRC}/lib/README.md" "${RUNTIME_VENDOR}/README.upstream.md"

sync_grammar_vendor "${C_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/c"
sync_grammar_vendor "${CPP_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/cpp"
sync_grammar_vendor "${GO_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/go"
sync_grammar_vendor "${BASH_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/bash"
sync_grammar_vendor "${HTML_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/html"
sync_grammar_vendor "${JAVASCRIPT_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/javascript"
sync_grammar_vendor "${JSDOC_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/jsdoc"
# TypeScript grammar keeps top-level queries/ separate from typescript/src/.
# Stage them into typescript/ so sync_grammar_vendor picks them up.
cp -R "${TYPESCRIPT_GRAMMAR_SRC}/queries" "${TYPESCRIPT_GRAMMAR_SRC}/typescript/queries"
sync_grammar_vendor "${TYPESCRIPT_GRAMMAR_SRC}/typescript" "${REPO_ROOT}/vendor/tree_sitter/grammars/typescript"
# scanner.c includes ../../common/scanner.h; place the shared common/ under
# typescript/ and repoint the include so each grammar owns its common/.
rm -rf "${REPO_ROOT}/vendor/tree_sitter/grammars/typescript/common"
cp -R "${TYPESCRIPT_GRAMMAR_SRC}/common" "${REPO_ROOT}/vendor/tree_sitter/grammars/typescript/common"
sed -i.bak 's|\.\./\.\./common/scanner\.h|../common/scanner.h|' \
	"${REPO_ROOT}/vendor/tree_sitter/grammars/typescript/src/scanner.c"
rm -f "${REPO_ROOT}/vendor/tree_sitter/grammars/typescript/src/scanner.c.bak"
sync_grammar_vendor "${CSS_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/css"
sync_grammar_vendor "${JSON_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/json"
sync_grammar_vendor "${PYTHON_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/python"
# PHP grammar keeps top-level queries/ separate from php/src/; stage them into
# php/ so sync_grammar_vendor picks them up.
cp -R "${PHP_GRAMMAR_SRC}/queries" "${PHP_GRAMMAR_SRC}/php/queries"
sync_grammar_vendor "${PHP_GRAMMAR_SRC}/php" "${REPO_ROOT}/vendor/tree_sitter/grammars/php"
# scanner.c includes ../../common/scanner.h; place the shared common/ under
# php/ and repoint the include so each grammar owns its common/.
rm -rf "${REPO_ROOT}/vendor/tree_sitter/grammars/php/common"
cp -R "${PHP_GRAMMAR_SRC}/common" "${REPO_ROOT}/vendor/tree_sitter/grammars/php/common"
sed -i.bak 's|\.\./\.\./common/scanner\.h|../common/scanner.h|' \
	"${REPO_ROOT}/vendor/tree_sitter/grammars/php/src/scanner.c"
rm -f "${REPO_ROOT}/vendor/tree_sitter/grammars/php/src/scanner.c.bak"
# Local fix for an lvalue use of array_pop() in the PHP scanner
git -C "${REPO_ROOT}" apply \
	"${REPO_ROOT}/vendor/tree_sitter/patches/php-scanner-array-pop-lvalue.patch"
# Add injection.include-children to PHP heredoc/nowdoc so injected HTML and
# language-tagged heredoc bodies highlight correctly through nested children.
git -C "${REPO_ROOT}" apply \
	"${REPO_ROOT}/vendor/tree_sitter/patches/php-injections-include-children.patch"
sync_grammar_vendor "${RUST_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/rust"
sync_grammar_vendor "${JAVA_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/java"
sync_grammar_vendor "${REGEX_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/regex"
sync_grammar_vendor "${CSHARP_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/csharp"
sync_grammar_vendor "${HASKELL_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/haskell"
sync_grammar_vendor "${RUBY_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/ruby"
# tree-sitter-ocaml keeps top-level queries/ separate from grammars/ocaml/src/.
# Stage them into the sub-grammar so sync_grammar_vendor picks them up.
cp -R "${OCAML_GRAMMAR_SRC}/queries" "${OCAML_GRAMMAR_SRC}/grammars/ocaml/queries"
sync_grammar_vendor "${OCAML_GRAMMAR_SRC}/grammars/ocaml" "${REPO_ROOT}/vendor/tree_sitter/grammars/ocaml"
# scanner.c includes ../../../common/scanner.h; place the shared common/ under
# ocaml/ and repoint the include so each grammar owns its common/.
rm -rf "${REPO_ROOT}/vendor/tree_sitter/grammars/ocaml/common"
cp -R "${OCAML_GRAMMAR_SRC}/common" "${REPO_ROOT}/vendor/tree_sitter/grammars/ocaml/common"
sed -i.bak 's|\.\./\.\./\.\./common/scanner\.h|../common/scanner.h|' \
	"${REPO_ROOT}/vendor/tree_sitter/grammars/ocaml/src/scanner.c"
rm -f "${REPO_ROOT}/vendor/tree_sitter/grammars/ocaml/src/scanner.c.bak"
sync_grammar_vendor "${JULIA_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/julia"
sync_grammar_vendor "${SCALA_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/scala"
# tree-sitter-embedded-template is shared between EJS and ERB; vendor as a single
# grammar dir and choose dialect-specific queries at runtime.
sync_grammar_vendor "${EMBEDDED_TEMPLATE_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/embedded_template"

echo "Tree-sitter vendor refresh complete." >&2
echo "If you changed refs/releases, update vendor/tree_sitter/VERSIONS.env and VERSIONS.md." >&2

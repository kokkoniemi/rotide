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
	# Larger tarballs would SIGPIPE tar when awk exits early; read the whole
	# listing first, then take the top-level dir from the buffered output.
	local listing
	listing="$(tar -tzf "${tarball}")"
	top="$(printf '%s\n' "${listing}" | awk -F/ 'NR == 1 { print $1; exit }')"
	tar -xzf "${tarball}" -C "${TMP_DIR}"

	printf -v "${out_var}" '%s' "${TMP_DIR}/${top}"
}

regenerate_parser() {
	local src_dir="$1"
	local name="$2"
	echo "Regenerating ${name} parser with official CLI binary" >&2
	(
		cd "${src_dir}"
		"${CLI_BIN}" generate --js-runtime native
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

# tree-sitter-vue ships an ES-module grammar.js that default-imports the
# CommonJS tree-sitter-html grammar. The CLI's QuickJS runtime cannot interop
# an ESM default import of a CommonJS module, so rewrite the two module lines
# (and package.json "type") to CommonJS require()/module.exports, matching the
# form tree-sitter-svelte already uses. Only the module wrappers change; the
# grammar rules are untouched.
convert_vue_grammar_to_cjs() {
	local src_dir="$1"
	sed -i.bak \
		-e "s|^import HTML from 'tree-sitter-html/grammar\.js';|const HTML = require('tree-sitter-html/grammar');|" \
		-e 's|^export default grammar(HTML, {|module.exports = grammar(HTML, {|' \
		"${src_dir}/grammar.js"
	rm -f "${src_dir}/grammar.js.bak"
	if [[ -f "${src_dir}/package.json" ]]; then
		sed -i.bak 's|"type": *"module"|"type": "commonjs"|' "${src_dir}/package.json"
		rm -f "${src_dir}/package.json.bak"
	fi
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
	elif [[ -f "${src_dir}/COPYING.txt" ]]; then
		cp "${src_dir}/COPYING.txt" "${vendor_dir}/LICENSE"
	elif [[ -f "${src_dir}/COPYING" ]]; then
		cp "${src_dir}/COPYING" "${vendor_dir}/LICENSE"
	fi
	if [[ -f "${src_dir}/README.md" ]]; then
		cp "${src_dir}/README.md" "${vendor_dir}/README.upstream.md"
	fi

	if [[ -d "${src_dir}/queries" ]]; then
		mkdir -p "${vendor_dir}/queries"
		cp -R "${src_dir}/queries/." "${vendor_dir}/queries/"
	fi
}

ONLY_GRAMMAR=""
if [[ $# -gt 0 ]]; then
	if [[ $# -ne 2 || "$1" != "--grammar" || \
		( "$2" != "bash" && "$2" != "bibtex" && "$2" != "clojure" && "$2" != "cpp" && "$2" != "csharp" && "$2" != "dockerfile" && "$2" != "glsl" && "$2" != "haskell" && "$2" != "hcl" && "$2" != "helm" && "$2" != "julia" && \
		"$2" != "kotlin" && "$2" != "latex" && "$2" != "lua" && "$2" != "ocaml" && "$2" != "php" && "$2" != "r" && "$2" != "ruby" && \
		"$2" != "rust" && "$2" != "scala" && "$2" != "svelte" && "$2" != "typescript" && "$2" != "vue" ) ]]; then
		echo "Usage: $0 [--grammar bash|bibtex|clojure|cpp|csharp|dockerfile|glsl|haskell|hcl|helm|julia|kotlin|latex|lua|ocaml|php|r|ruby|rust|scala|svelte|typescript|vue]" >&2
		exit 2
	fi
	ONLY_GRAMMAR="$2"
fi

download_cli

if [[ "${ONLY_GRAMMAR}" == "bash" ]]; then
	BASH_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-bash" \
		"${TREE_SITTER_BASH_GRAMMAR_REF}" BASH_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/bash/grammar.js" \
		"${BASH_GRAMMAR_SRC}/grammar.js"
	regenerate_parser "${BASH_GRAMMAR_SRC}" "Bash"
	sync_grammar_vendor "${BASH_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/bash"
	echo "Tree-sitter Bash vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "cpp" ]]; then
	CPP_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-cpp" \
		"${TREE_SITTER_CPP_GRAMMAR_REF}" CPP_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/cpp/grammar.js" \
		"${CPP_GRAMMAR_SRC}/grammar.js"
	regenerate_parser "${CPP_GRAMMAR_SRC}" "C++"
	sync_grammar_vendor "${CPP_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/cpp"
	echo "Tree-sitter C++ vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "csharp" ]]; then
	CSHARP_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-c-sharp" \
		"${TREE_SITTER_CSHARP_GRAMMAR_REF}" CSHARP_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/csharp/grammar.js" \
		"${CSHARP_GRAMMAR_SRC}/grammar.js"
	regenerate_parser "${CSHARP_GRAMMAR_SRC}" "C#"
	rm -f "${CSHARP_GRAMMAR_SRC}/src/scanner.c"
	sync_grammar_vendor "${CSHARP_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/csharp"
	echo "Tree-sitter C# vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "julia" ]]; then
	JULIA_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-julia" \
		"${TREE_SITTER_JULIA_GRAMMAR_REF}" JULIA_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/julia/grammar.js" \
		"${JULIA_GRAMMAR_SRC}/grammar.js"
	regenerate_parser "${JULIA_GRAMMAR_SRC}" "Julia"
	rm -f "${JULIA_GRAMMAR_SRC}/src/scanner.c"
	sync_grammar_vendor "${JULIA_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/julia"
	echo "Tree-sitter Julia vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "haskell" ]]; then
	HASKELL_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-haskell" \
		"${TREE_SITTER_HASKELL_GRAMMAR_REF}" HASKELL_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/haskell/grammar.js" \
		"${HASKELL_GRAMMAR_SRC}/grammar.js"
	regenerate_parser "${HASKELL_GRAMMAR_SRC}" "Haskell"
	rm -f "${HASKELL_GRAMMAR_SRC}/src/scanner.c" "${HASKELL_GRAMMAR_SRC}/src/unicode.h"
	sync_grammar_vendor "${HASKELL_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/haskell"
	echo "Tree-sitter Haskell vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "bibtex" ]]; then
	BIBTEX_GRAMMAR_SRC=""
	download_repo_tarball "latex-lsp/tree-sitter-bibtex" \
		"${TREE_SITTER_BIBTEX_GRAMMAR_REF}" BIBTEX_GRAMMAR_SRC
	regenerate_parser "${BIBTEX_GRAMMAR_SRC}" "BibTeX"
	sync_grammar_vendor "${BIBTEX_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/bibtex"
	echo "Tree-sitter BibTeX vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "helm" ]]; then
	# helm is a self-authored, in-repo grammar (no upstream tarball). Its
	# grammar.js lives in the vendored dir; just regenerate parser.c in place.
	regenerate_parser "${REPO_ROOT}/vendor/tree_sitter/grammars/helm" "Helm"
	echo "Tree-sitter Helm parser regeneration complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "hcl" ]]; then
	HCL_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter-grammars/tree-sitter-hcl" \
		"${TREE_SITTER_HCL_GRAMMAR_REF}" HCL_GRAMMAR_SRC
	regenerate_parser "${HCL_GRAMMAR_SRC}" "HCL"
	sync_grammar_vendor "${HCL_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/hcl"
	echo "Tree-sitter HCL vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "lua" ]]; then
	LUA_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter-grammars/tree-sitter-lua" \
		"${TREE_SITTER_LUA_GRAMMAR_REF}" LUA_GRAMMAR_SRC
	regenerate_parser "${LUA_GRAMMAR_SRC}" "Lua"
	sync_grammar_vendor "${LUA_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/lua"
	echo "Tree-sitter Lua vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "glsl" ]]; then
	GLSL_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter-grammars/tree-sitter-glsl" \
		"${TREE_SITTER_GLSL_GRAMMAR_REF}" GLSL_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/glsl/grammar.js" \
		"${GLSL_GRAMMAR_SRC}/grammar.js"
	regenerate_parser "${GLSL_GRAMMAR_SRC}" "GLSL"
	sync_grammar_vendor "${GLSL_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/glsl"
	echo "Tree-sitter GLSL vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "kotlin" ]]; then
	KOTLIN_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter-grammars/tree-sitter-kotlin" \
		"${TREE_SITTER_KOTLIN_GRAMMAR_REF}" KOTLIN_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/kotlin/grammar.js" \
		"${KOTLIN_GRAMMAR_SRC}/grammar.js"
	regenerate_parser "${KOTLIN_GRAMMAR_SRC}" "Kotlin"
	rm -f "${KOTLIN_GRAMMAR_SRC}/src/scanner.c"
	sync_grammar_vendor "${KOTLIN_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/kotlin"
	echo "Tree-sitter Kotlin vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "svelte" ]]; then
	HTML_GRAMMAR_SRC=""
	SVELTE_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-html" \
		"${TREE_SITTER_HTML_GRAMMAR_REF}" HTML_GRAMMAR_SRC
	download_repo_tarball "tree-sitter-grammars/tree-sitter-svelte" \
		"${TREE_SITTER_SVELTE_GRAMMAR_REF}" SVELTE_GRAMMAR_SRC
	# grammar.js extends tree-sitter-html via require('tree-sitter-html/grammar');
	# expose the pinned HTML source in node_modules before regenerating.
	link_grammar_dep "${SVELTE_GRAMMAR_SRC}" "tree-sitter-html" "${HTML_GRAMMAR_SRC}"
	regenerate_parser "${SVELTE_GRAMMAR_SRC}" "Svelte"
	sync_grammar_vendor "${SVELTE_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/svelte"
	echo "Tree-sitter Svelte vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "vue" ]]; then
	HTML_GRAMMAR_SRC=""
	VUE_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-html" \
		"${TREE_SITTER_HTML_GRAMMAR_REF}" HTML_GRAMMAR_SRC
	download_repo_tarball "tree-sitter-grammars/tree-sitter-vue" \
		"${TREE_SITTER_VUE_GRAMMAR_REF}" VUE_GRAMMAR_SRC
	# grammar.js extends tree-sitter-html via `import HTML from
	# 'tree-sitter-html/grammar.js'`; expose the pinned HTML source in
	# node_modules before regenerating.
	link_grammar_dep "${VUE_GRAMMAR_SRC}" "tree-sitter-html" "${HTML_GRAMMAR_SRC}"
	convert_vue_grammar_to_cjs "${VUE_GRAMMAR_SRC}"
	regenerate_parser "${VUE_GRAMMAR_SRC}" "Vue"
	sync_grammar_vendor "${VUE_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/vue"
	echo "Tree-sitter Vue vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "dockerfile" ]]; then
	DOCKERFILE_GRAMMAR_SRC=""
	download_repo_tarball "wharflab/tree-sitter-containerfile" \
		"${TREE_SITTER_DOCKERFILE_GRAMMAR_REF}" DOCKERFILE_GRAMMAR_SRC
	regenerate_parser "${DOCKERFILE_GRAMMAR_SRC}" "Containerfile"
	sync_grammar_vendor "${DOCKERFILE_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/dockerfile"
	echo "Tree-sitter Dockerfile (containerfile) vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "clojure" ]]; then
	CLOJURE_GRAMMAR_SRC=""
	download_repo_tarball "sogaiu/tree-sitter-clojure" \
		"${TREE_SITTER_CLOJURE_GRAMMAR_REF}" CLOJURE_GRAMMAR_SRC
	regenerate_parser "${CLOJURE_GRAMMAR_SRC}" "Clojure"
	sync_grammar_vendor "${CLOJURE_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/clojure"
	echo "Tree-sitter Clojure vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "r" ]]; then
	R_GRAMMAR_SRC=""
	download_repo_tarball "r-lib/tree-sitter-r" \
		"${TREE_SITTER_R_GRAMMAR_REF}" R_GRAMMAR_SRC
	regenerate_parser "${R_GRAMMAR_SRC}" "R"
	sync_grammar_vendor "${R_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/r"
	echo "Tree-sitter R vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "latex" ]]; then
	LATEX_GRAMMAR_SRC=""
	download_repo_tarball "latex-lsp/tree-sitter-latex" \
		"${TREE_SITTER_LATEX_GRAMMAR_REF}" LATEX_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/latex/grammar.js" \
		"${LATEX_GRAMMAR_SRC}/grammar.js"
	regenerate_parser "${LATEX_GRAMMAR_SRC}" "LaTeX"
	rm -f "${LATEX_GRAMMAR_SRC}/src/scanner.c"
	sync_grammar_vendor "${LATEX_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/latex"
	echo "Tree-sitter LaTeX vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "ocaml" ]]; then
	OCAML_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-ocaml" \
		"${TREE_SITTER_OCAML_GRAMMAR_REF}" OCAML_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/ocaml/grammar.js" \
		"${OCAML_GRAMMAR_SRC}/grammars/ocaml/grammar.js"
	regenerate_parser "${OCAML_GRAMMAR_SRC}/grammars/ocaml" "OCaml"
	rm -f "${OCAML_GRAMMAR_SRC}/grammars/ocaml/src/scanner.c"
	cp -R "${OCAML_GRAMMAR_SRC}/queries" \
		"${OCAML_GRAMMAR_SRC}/grammars/ocaml/queries"
	sync_grammar_vendor "${OCAML_GRAMMAR_SRC}/grammars/ocaml" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/ocaml"
	rm -rf "${REPO_ROOT}/vendor/tree_sitter/grammars/ocaml/common"
	echo "Tree-sitter OCaml vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "php" ]]; then
	PHP_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-php" \
		"${TREE_SITTER_PHP_GRAMMAR_REF}" PHP_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/php/grammar.js" \
		"${PHP_GRAMMAR_SRC}/php/grammar.js"
	regenerate_parser "${PHP_GRAMMAR_SRC}/php" "PHP"
	cp -R "${PHP_GRAMMAR_SRC}/queries" "${PHP_GRAMMAR_SRC}/php/queries"
	sync_grammar_vendor "${PHP_GRAMMAR_SRC}/php" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/php"
	rm -rf "${REPO_ROOT}/vendor/tree_sitter/grammars/php/common"
	cp -R "${PHP_GRAMMAR_SRC}/common" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/php/common"
	sed -i.bak 's|\.\./\.\./common/scanner\.h|../common/scanner.h|' \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/php/src/scanner.c"
	rm -f "${REPO_ROOT}/vendor/tree_sitter/grammars/php/src/scanner.c.bak"
	git -C "${REPO_ROOT}" apply \
		"${REPO_ROOT}/vendor/tree_sitter/patches/php-scanner-array-pop-lvalue.patch"
	git -C "${REPO_ROOT}" apply \
		"${REPO_ROOT}/vendor/tree_sitter/patches/php-injections-include-children.patch"
	echo "Tree-sitter PHP vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "typescript" ]]; then
	JAVASCRIPT_GRAMMAR_SRC=""
	TYPESCRIPT_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-javascript" \
		"${TREE_SITTER_JAVASCRIPT_GRAMMAR_REF}" JAVASCRIPT_GRAMMAR_SRC
	download_repo_tarball "tree-sitter/tree-sitter-typescript" \
		"${TREE_SITTER_TYPESCRIPT_GRAMMAR_REF}" TYPESCRIPT_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/typescript/define-grammar.js" \
		"${TYPESCRIPT_GRAMMAR_SRC}/common/define-grammar.js"
	link_grammar_dep "${TYPESCRIPT_GRAMMAR_SRC}" "tree-sitter-javascript" \
		"${JAVASCRIPT_GRAMMAR_SRC}"
	regenerate_parser "${TYPESCRIPT_GRAMMAR_SRC}/typescript" "TypeScript"
	regenerate_parser "${TYPESCRIPT_GRAMMAR_SRC}/tsx" "TSX"
	cp -R "${TYPESCRIPT_GRAMMAR_SRC}/queries" \
		"${TYPESCRIPT_GRAMMAR_SRC}/typescript/queries"
	sync_grammar_vendor "${TYPESCRIPT_GRAMMAR_SRC}/typescript" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/typescript"
	cp -R "${TYPESCRIPT_GRAMMAR_SRC}/queries" "${TYPESCRIPT_GRAMMAR_SRC}/tsx/queries"
	sync_grammar_vendor "${TYPESCRIPT_GRAMMAR_SRC}/tsx" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/tsx"
	for grammar_name in typescript tsx; do
		rm -rf "${REPO_ROOT}/vendor/tree_sitter/grammars/${grammar_name}/common"
		cp -R "${TYPESCRIPT_GRAMMAR_SRC}/common" \
			"${REPO_ROOT}/vendor/tree_sitter/grammars/${grammar_name}/common"
		sed -i.bak 's|\.\./\.\./common/scanner\.h|../common/scanner.h|' \
			"${REPO_ROOT}/vendor/tree_sitter/grammars/${grammar_name}/src/scanner.c"
		rm -f "${REPO_ROOT}/vendor/tree_sitter/grammars/${grammar_name}/src/scanner.c.bak"
	done
	echo "Tree-sitter TypeScript/TSX vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "ruby" ]]; then
	RUBY_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-ruby" \
		"${TREE_SITTER_RUBY_GRAMMAR_REF}" RUBY_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/ruby/grammar.js" \
		"${RUBY_GRAMMAR_SRC}/grammar.js"
	regenerate_parser "${RUBY_GRAMMAR_SRC}" "Ruby"
	rm -f "${RUBY_GRAMMAR_SRC}/src/scanner.c"
	sync_grammar_vendor "${RUBY_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/ruby"
	echo "Tree-sitter Ruby vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "rust" ]]; then
	RUST_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-rust" \
		"${TREE_SITTER_RUST_GRAMMAR_REF}" RUST_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/rust/grammar.js" \
		"${RUST_GRAMMAR_SRC}/grammar.js"
	regenerate_parser "${RUST_GRAMMAR_SRC}" "Rust"
	sync_grammar_vendor "${RUST_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/rust"
	echo "Tree-sitter Rust vendor refresh complete." >&2
	exit 0
fi

if [[ "${ONLY_GRAMMAR}" == "scala" ]]; then
	SCALA_GRAMMAR_SRC=""
	download_repo_tarball "tree-sitter/tree-sitter-scala" \
		"${TREE_SITTER_SCALA_GRAMMAR_REF}" SCALA_GRAMMAR_SRC
	cp "${REPO_ROOT}/vendor/tree_sitter/overrides/scala/grammar.js" \
		"${SCALA_GRAMMAR_SRC}/grammar.js"
	regenerate_parser "${SCALA_GRAMMAR_SRC}" "Scala"
	sync_grammar_vendor "${SCALA_GRAMMAR_SRC}" \
		"${REPO_ROOT}/vendor/tree_sitter/grammars/scala"
	echo "Tree-sitter Scala vendor refresh complete." >&2
	exit 0
fi

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
MARKDOWN_GRAMMAR_SRC=""
TOML_GRAMMAR_SRC=""
YAML_GRAMMAR_SRC=""
XML_GRAMMAR_SRC=""
MAKE_GRAMMAR_SRC=""
DIFF_GRAMMAR_SRC=""
LATEX_GRAMMAR_SRC=""
BIBTEX_GRAMMAR_SRC=""
HCL_GRAMMAR_SRC=""
LUA_GRAMMAR_SRC=""
GLSL_GRAMMAR_SRC=""
KOTLIN_GRAMMAR_SRC=""
SVELTE_GRAMMAR_SRC=""
VUE_GRAMMAR_SRC=""
DOCKERFILE_GRAMMAR_SRC=""
CLOJURE_GRAMMAR_SRC=""
R_GRAMMAR_SRC=""

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
download_repo_tarball "tree-sitter-grammars/tree-sitter-markdown" "${TREE_SITTER_MARKDOWN_GRAMMAR_REF}" MARKDOWN_GRAMMAR_SRC
download_repo_tarball "tree-sitter-grammars/tree-sitter-toml" "${TREE_SITTER_TOML_GRAMMAR_REF}" TOML_GRAMMAR_SRC
download_repo_tarball "tree-sitter-grammars/tree-sitter-yaml" "${TREE_SITTER_YAML_GRAMMAR_REF}" YAML_GRAMMAR_SRC
download_repo_tarball "tree-sitter-grammars/tree-sitter-xml" "${TREE_SITTER_XML_GRAMMAR_REF}" XML_GRAMMAR_SRC
download_repo_tarball "tree-sitter-grammars/tree-sitter-make" "${TREE_SITTER_MAKE_GRAMMAR_REF}" MAKE_GRAMMAR_SRC
download_repo_tarball "tree-sitter-grammars/tree-sitter-diff" "${TREE_SITTER_DIFF_GRAMMAR_REF}" DIFF_GRAMMAR_SRC
download_repo_tarball "latex-lsp/tree-sitter-latex" "${TREE_SITTER_LATEX_GRAMMAR_REF}" LATEX_GRAMMAR_SRC
download_repo_tarball "latex-lsp/tree-sitter-bibtex" "${TREE_SITTER_BIBTEX_GRAMMAR_REF}" BIBTEX_GRAMMAR_SRC
download_repo_tarball "tree-sitter-grammars/tree-sitter-hcl" "${TREE_SITTER_HCL_GRAMMAR_REF}" HCL_GRAMMAR_SRC
download_repo_tarball "tree-sitter-grammars/tree-sitter-lua" "${TREE_SITTER_LUA_GRAMMAR_REF}" LUA_GRAMMAR_SRC
download_repo_tarball "tree-sitter-grammars/tree-sitter-glsl" "${TREE_SITTER_GLSL_GRAMMAR_REF}" GLSL_GRAMMAR_SRC
download_repo_tarball "tree-sitter-grammars/tree-sitter-kotlin" "${TREE_SITTER_KOTLIN_GRAMMAR_REF}" KOTLIN_GRAMMAR_SRC
download_repo_tarball "tree-sitter-grammars/tree-sitter-svelte" "${TREE_SITTER_SVELTE_GRAMMAR_REF}" SVELTE_GRAMMAR_SRC
download_repo_tarball "tree-sitter-grammars/tree-sitter-vue" "${TREE_SITTER_VUE_GRAMMAR_REF}" VUE_GRAMMAR_SRC
download_repo_tarball "wharflab/tree-sitter-containerfile" "${TREE_SITTER_DOCKERFILE_GRAMMAR_REF}" DOCKERFILE_GRAMMAR_SRC
download_repo_tarball "sogaiu/tree-sitter-clojure" "${TREE_SITTER_CLOJURE_GRAMMAR_REF}" CLOJURE_GRAMMAR_SRC
download_repo_tarball "r-lib/tree-sitter-r" "${TREE_SITTER_R_GRAMMAR_REF}" R_GRAMMAR_SRC

if [[ ! -d "${RUNTIME_SRC}/lib/src" || ! -f "${RUNTIME_SRC}/lib/include/tree_sitter/api.h" ]]; then
	echo "Runtime source layout not found in ${TREE_SITTER_RUNTIME_REF}" >&2
	exit 1
fi

regenerate_parser "${C_GRAMMAR_SRC}" "C"
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/cpp/grammar.js" \
	"${CPP_GRAMMAR_SRC}/grammar.js"
regenerate_parser "${CPP_GRAMMAR_SRC}" "C++"
regenerate_parser "${GO_GRAMMAR_SRC}" "Go"
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/bash/grammar.js" \
	"${BASH_GRAMMAR_SRC}/grammar.js"
regenerate_parser "${BASH_GRAMMAR_SRC}" "Bash"
regenerate_parser "${HTML_GRAMMAR_SRC}" "HTML"
regenerate_parser "${JAVASCRIPT_GRAMMAR_SRC}" "JavaScript"
regenerate_parser "${JSDOC_GRAMMAR_SRC}" "JSDoc"
# tree-sitter-typescript grammar.js requires tree-sitter-javascript via
# common/define-grammar.js; expose the pinned JS source in node_modules.
link_grammar_dep "${TYPESCRIPT_GRAMMAR_SRC}" "tree-sitter-javascript" "${JAVASCRIPT_GRAMMAR_SRC}"
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/typescript/define-grammar.js" \
	"${TYPESCRIPT_GRAMMAR_SRC}/common/define-grammar.js"
regenerate_parser "${TYPESCRIPT_GRAMMAR_SRC}/typescript" "TypeScript"
regenerate_parser "${TYPESCRIPT_GRAMMAR_SRC}/tsx" "TSX"
regenerate_parser "${CSS_GRAMMAR_SRC}" "CSS"
regenerate_parser "${JSON_GRAMMAR_SRC}" "JSON"
regenerate_parser "${PYTHON_GRAMMAR_SRC}" "Python"
# tree-sitter-php grammar.js requires ../common/define-grammar.js inside the
# tarball layout; regenerate from the php/ sub-grammar (HTML-mixed variant).
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/php/grammar.js" \
	"${PHP_GRAMMAR_SRC}/php/grammar.js"
regenerate_parser "${PHP_GRAMMAR_SRC}/php" "PHP"
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/rust/grammar.js" \
	"${RUST_GRAMMAR_SRC}/grammar.js"
regenerate_parser "${RUST_GRAMMAR_SRC}" "Rust"
regenerate_parser "${JAVA_GRAMMAR_SRC}" "Java"
regenerate_parser "${REGEX_GRAMMAR_SRC}" "Regex"
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/csharp/grammar.js" \
	"${CSHARP_GRAMMAR_SRC}/grammar.js"
regenerate_parser "${CSHARP_GRAMMAR_SRC}" "C#"
rm -f "${CSHARP_GRAMMAR_SRC}/src/scanner.c"
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/haskell/grammar.js" \
	"${HASKELL_GRAMMAR_SRC}/grammar.js"
regenerate_parser "${HASKELL_GRAMMAR_SRC}" "Haskell"
rm -f "${HASKELL_GRAMMAR_SRC}/src/scanner.c" "${HASKELL_GRAMMAR_SRC}/src/unicode.h"
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/ruby/grammar.js" \
	"${RUBY_GRAMMAR_SRC}/grammar.js"
regenerate_parser "${RUBY_GRAMMAR_SRC}" "Ruby"
rm -f "${RUBY_GRAMMAR_SRC}/src/scanner.c"
# tree-sitter-ocaml ships sub-grammars under grammars/<name>/ (ocaml, interface,
# type). Only the ocaml sub-grammar is vendored; regenerate from there.
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/ocaml/grammar.js" \
	"${OCAML_GRAMMAR_SRC}/grammars/ocaml/grammar.js"
regenerate_parser "${OCAML_GRAMMAR_SRC}/grammars/ocaml" "OCaml"
rm -f "${OCAML_GRAMMAR_SRC}/grammars/ocaml/src/scanner.c"
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/julia/grammar.js" \
	"${JULIA_GRAMMAR_SRC}/grammar.js"
regenerate_parser "${JULIA_GRAMMAR_SRC}" "Julia"
rm -f "${JULIA_GRAMMAR_SRC}/src/scanner.c"
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/scala/grammar.js" \
	"${SCALA_GRAMMAR_SRC}/grammar.js"
regenerate_parser "${SCALA_GRAMMAR_SRC}" "Scala"
regenerate_parser "${EMBEDDED_TEMPLATE_GRAMMAR_SRC}" "embedded-template"
# tree-sitter-grammars/tree-sitter-markdown ships block (tree-sitter-markdown/)
# and inline (tree-sitter-markdown-inline/) sub-grammars whose grammar.js files
# require ../common/common.js. The tarball preserves that layout so each
# sub-grammar regenerates in place.
regenerate_parser "${MARKDOWN_GRAMMAR_SRC}/tree-sitter-markdown" "Markdown"
regenerate_parser "${MARKDOWN_GRAMMAR_SRC}/tree-sitter-markdown-inline" "Markdown Inline"
regenerate_parser "${TOML_GRAMMAR_SRC}" "TOML"
regenerate_parser "${YAML_GRAMMAR_SRC}" "YAML"
regenerate_parser "${XML_GRAMMAR_SRC}/xml" "XML"
regenerate_parser "${MAKE_GRAMMAR_SRC}" "Make"
regenerate_parser "${DIFF_GRAMMAR_SRC}" "Diff"
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/latex/grammar.js" \
	"${LATEX_GRAMMAR_SRC}/grammar.js"
regenerate_parser "${LATEX_GRAMMAR_SRC}" "LaTeX"
rm -f "${LATEX_GRAMMAR_SRC}/src/scanner.c"
regenerate_parser "${BIBTEX_GRAMMAR_SRC}" "BibTeX"
regenerate_parser "${HCL_GRAMMAR_SRC}" "HCL"
regenerate_parser "${LUA_GRAMMAR_SRC}" "Lua"
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/glsl/grammar.js" \
	"${GLSL_GRAMMAR_SRC}/grammar.js"
regenerate_parser "${GLSL_GRAMMAR_SRC}" "GLSL"
cp "${REPO_ROOT}/vendor/tree_sitter/overrides/kotlin/grammar.js" \
	"${KOTLIN_GRAMMAR_SRC}/grammar.js"
regenerate_parser "${KOTLIN_GRAMMAR_SRC}" "Kotlin"
rm -f "${KOTLIN_GRAMMAR_SRC}/src/scanner.c"
# tree-sitter-svelte grammar.js extends tree-sitter-html via
# require('tree-sitter-html/grammar'); expose the pinned JS source in
# node_modules before regenerating.
link_grammar_dep "${SVELTE_GRAMMAR_SRC}" "tree-sitter-html" "${HTML_GRAMMAR_SRC}"
regenerate_parser "${SVELTE_GRAMMAR_SRC}" "Svelte"
# tree-sitter-vue grammar.js also extends tree-sitter-html (ESM import); link
# the pinned JS source in node_modules before regenerating.
link_grammar_dep "${VUE_GRAMMAR_SRC}" "tree-sitter-html" "${HTML_GRAMMAR_SRC}"
convert_vue_grammar_to_cjs "${VUE_GRAMMAR_SRC}"
regenerate_parser "${VUE_GRAMMAR_SRC}" "Vue"
# helm is a self-authored, in-repo grammar (no upstream tarball); regenerate
# parser.c in place from its committed grammar.js.
regenerate_parser "${REPO_ROOT}/vendor/tree_sitter/grammars/helm" "Helm"
# wharflab/tree-sitter-containerfile: grammar/parser is "containerfile"; RotIDE
# vendors it under grammars/dockerfile. Ships an external scanner.
regenerate_parser "${DOCKERFILE_GRAMMAR_SRC}" "Containerfile"
# sogaiu/tree-sitter-clojure: parser-only, no external scanner.
regenerate_parser "${CLOJURE_GRAMMAR_SRC}" "Clojure"
# r-lib/tree-sitter-r: ships an external scanner.
regenerate_parser "${R_GRAMMAR_SRC}" "R"

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
cp -R "${TYPESCRIPT_GRAMMAR_SRC}/queries" "${TYPESCRIPT_GRAMMAR_SRC}/tsx/queries"
sync_grammar_vendor "${TYPESCRIPT_GRAMMAR_SRC}/tsx" "${REPO_ROOT}/vendor/tree_sitter/grammars/tsx"
# scanner.c includes ../../common/scanner.h; place the shared common/ under
# typescript/ and repoint the include so each grammar owns its common/.
rm -rf "${REPO_ROOT}/vendor/tree_sitter/grammars/typescript/common"
cp -R "${TYPESCRIPT_GRAMMAR_SRC}/common" "${REPO_ROOT}/vendor/tree_sitter/grammars/typescript/common"
sed -i.bak 's|\.\./\.\./common/scanner\.h|../common/scanner.h|' \
	"${REPO_ROOT}/vendor/tree_sitter/grammars/typescript/src/scanner.c"
rm -f "${REPO_ROOT}/vendor/tree_sitter/grammars/typescript/src/scanner.c.bak"
rm -rf "${REPO_ROOT}/vendor/tree_sitter/grammars/tsx/common"
cp -R "${TYPESCRIPT_GRAMMAR_SRC}/common" "${REPO_ROOT}/vendor/tree_sitter/grammars/tsx/common"
sed -i.bak 's|\.\./\.\./common/scanner\.h|../common/scanner.h|' \
	"${REPO_ROOT}/vendor/tree_sitter/grammars/tsx/src/scanner.c"
rm -f "${REPO_ROOT}/vendor/tree_sitter/grammars/tsx/src/scanner.c.bak"
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
rm -rf "${REPO_ROOT}/vendor/tree_sitter/grammars/ocaml/common"
sync_grammar_vendor "${JULIA_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/julia"
sync_grammar_vendor "${SCALA_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/scala"
# tree-sitter-embedded-template is shared between EJS and ERB; vendor as a single
# grammar dir and choose dialect-specific queries at runtime.
sync_grammar_vendor "${EMBEDDED_TEMPLATE_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/embedded_template"
# Generated parser.c is standalone; the shared common/ at the repo root is only
# used at generation time and is not vendored.
sync_grammar_vendor "${MARKDOWN_GRAMMAR_SRC}/tree-sitter-markdown" "${REPO_ROOT}/vendor/tree_sitter/grammars/markdown"
sync_grammar_vendor "${MARKDOWN_GRAMMAR_SRC}/tree-sitter-markdown-inline" "${REPO_ROOT}/vendor/tree_sitter/grammars/markdown_inline"
sync_grammar_vendor "${TOML_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/toml"
sync_grammar_vendor "${YAML_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/yaml"
# tree-sitter-xml ships XML and DTD sub-grammars plus shared common/scanner.h.
# RotIDE vendors the XML sub-grammar here; DTD can be added as a separate
# runtime language if file detection support is needed later.
rm -rf "${XML_GRAMMAR_SRC}/xml/queries"
mkdir -p "${XML_GRAMMAR_SRC}/xml/queries"
cp -R "${XML_GRAMMAR_SRC}/queries/xml/." "${XML_GRAMMAR_SRC}/xml/queries/"
sync_grammar_vendor "${XML_GRAMMAR_SRC}/xml" "${REPO_ROOT}/vendor/tree_sitter/grammars/xml"
rm -rf "${REPO_ROOT}/vendor/tree_sitter/grammars/xml/common"
cp -R "${XML_GRAMMAR_SRC}/common" "${REPO_ROOT}/vendor/tree_sitter/grammars/xml/common"
sed -i.bak \
	-e 's|\.\./\.\./common/scanner\.h|../common/scanner.h|' \
	-e 's|\.\./common/scanner\.h|../common/scanner.h|' \
	"${REPO_ROOT}/vendor/tree_sitter/grammars/xml/src/scanner.c"
rm -f "${REPO_ROOT}/vendor/tree_sitter/grammars/xml/src/scanner.c.bak"
# Preserve XML scanner tag-stack state during Tree-sitter serialization.
git -C "${REPO_ROOT}" apply \
	"${REPO_ROOT}/vendor/tree_sitter/patches/xml-scanner-serialize-preserve-tags.patch"
sync_grammar_vendor "${MAKE_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/make"
sync_grammar_vendor "${DIFF_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/diff"
sync_grammar_vendor "${LATEX_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/latex"
sync_grammar_vendor "${BIBTEX_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/bibtex"
sync_grammar_vendor "${HCL_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/hcl"
sync_grammar_vendor "${LUA_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/lua"
sync_grammar_vendor "${GLSL_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/glsl"
sync_grammar_vendor "${KOTLIN_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/kotlin"
sync_grammar_vendor "${SVELTE_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/svelte"
sync_grammar_vendor "${VUE_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/vue"
sync_grammar_vendor "${DOCKERFILE_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/dockerfile"
sync_grammar_vendor "${CLOJURE_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/clojure"
sync_grammar_vendor "${R_GRAMMAR_SRC}" "${REPO_ROOT}/vendor/tree_sitter/grammars/r"

echo "Tree-sitter vendor refresh complete." >&2
echo "If you changed refs/releases, update vendor/tree_sitter/VERSIONS.env and VERSIONS.md." >&2

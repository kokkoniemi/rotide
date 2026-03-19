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

echo "Fetching CLI release metadata: ${TREE_SITTER_CLI_RELEASE}" >&2
RELEASE_JSON="${TMP_DIR}/tree-sitter-release.json"
curl -fsSL "${CLI_RELEASE_API}" > "${RELEASE_JSON}"

CLI_URL="$(jq -r --arg asset "${CLI_ASSET}" '.assets[] | select(.name == $asset) | .browser_download_url' "${RELEASE_JSON}")"
CLI_DIGEST="$(jq -r --arg asset "${CLI_ASSET}" '.assets[] | select(.name == $asset) | .digest' "${RELEASE_JSON}")"

if [[ -z "${CLI_URL}" || "${CLI_URL}" == "null" ]]; then
	echo "Could not find CLI asset '${CLI_ASSET}' in ${TREE_SITTER_CLI_RELEASE}" >&2
	exit 1
fi
if [[ -z "${CLI_DIGEST}" || "${CLI_DIGEST}" == "null" || "${CLI_DIGEST}" != sha256:* ]]; then
	echo "Could not read sha256 digest for '${CLI_ASSET}' from release metadata" >&2
	exit 1
fi

CLI_ZIP="${TMP_DIR}/${CLI_ASSET}"
echo "Downloading CLI asset: ${CLI_ASSET}" >&2
curl -fsSL "${CLI_URL}" -o "${CLI_ZIP}"

EXPECTED_SHA256="${CLI_DIGEST#sha256:}"
ACTUAL_SHA256="$(sha256sum "${CLI_ZIP}" | awk '{print $1}')"
if [[ "${ACTUAL_SHA256}" != "${EXPECTED_SHA256}" ]]; then
	echo "Checksum mismatch for ${CLI_ASSET}" >&2
	echo "Expected: ${EXPECTED_SHA256}" >&2
	echo "Actual:   ${ACTUAL_SHA256}" >&2
	exit 1
fi

CLI_DIR="${TMP_DIR}/cli"
mkdir -p "${CLI_DIR}"
unzip -q "${CLI_ZIP}" -d "${CLI_DIR}"
CLI_BIN="$(find "${CLI_DIR}" -type f \( -name tree-sitter -o -name tree-sitter.exe \) | head -n 1)"
if [[ -z "${CLI_BIN}" ]]; then
	echo "Could not locate tree-sitter CLI binary after unzip" >&2
	exit 1
fi
chmod +x "${CLI_BIN}"

echo "Downloading runtime source ref: ${TREE_SITTER_RUNTIME_REF}" >&2
RUNTIME_TARBALL="${TMP_DIR}/tree-sitter-runtime.tar.gz"
curl -fsSL "https://github.com/tree-sitter/tree-sitter/archive/${TREE_SITTER_RUNTIME_REF}.tar.gz" -o "${RUNTIME_TARBALL}"
RUNTIME_TOP="$(tar -tzf "${RUNTIME_TARBALL}" | head -n 1 | cut -d/ -f1)"
tar -xzf "${RUNTIME_TARBALL}" -C "${TMP_DIR}"
RUNTIME_SRC="${TMP_DIR}/${RUNTIME_TOP}"

if [[ ! -d "${RUNTIME_SRC}/lib/src" || ! -f "${RUNTIME_SRC}/lib/include/tree_sitter/api.h" ]]; then
	echo "Runtime source layout not found in ${TREE_SITTER_RUNTIME_REF}" >&2
	exit 1
fi

echo "Downloading C grammar source ref: ${TREE_SITTER_C_GRAMMAR_REF}" >&2
GRAMMAR_TARBALL="${TMP_DIR}/tree-sitter-c.tar.gz"
curl -fsSL "https://github.com/tree-sitter/tree-sitter-c/archive/${TREE_SITTER_C_GRAMMAR_REF}.tar.gz" -o "${GRAMMAR_TARBALL}"
GRAMMAR_TOP="$(tar -tzf "${GRAMMAR_TARBALL}" | head -n 1 | cut -d/ -f1)"
tar -xzf "${GRAMMAR_TARBALL}" -C "${TMP_DIR}"
GRAMMAR_SRC="${TMP_DIR}/${GRAMMAR_TOP}"

if [[ ! -d "${GRAMMAR_SRC}/src" || ! -f "${GRAMMAR_SRC}/grammar.js" ]]; then
	echo "Grammar source layout not found in ${TREE_SITTER_C_GRAMMAR_REF}" >&2
	exit 1
fi

echo "Regenerating C parser with official CLI binary" >&2
(
	cd "${GRAMMAR_SRC}"
	"${CLI_BIN}" generate
)

RUNTIME_VENDOR="${REPO_ROOT}/vendor/tree_sitter/runtime"
GRAMMAR_VENDOR="${REPO_ROOT}/vendor/tree_sitter/grammars/c"

mkdir -p "${RUNTIME_VENDOR}/include/tree_sitter" "${RUNTIME_VENDOR}/src"
rm -rf "${RUNTIME_VENDOR}/include/tree_sitter" "${RUNTIME_VENDOR}/src"
mkdir -p "${RUNTIME_VENDOR}/include/tree_sitter" "${RUNTIME_VENDOR}/src"
cp -R "${RUNTIME_SRC}/lib/src/." "${RUNTIME_VENDOR}/src/"
cp "${RUNTIME_SRC}/lib/include/tree_sitter/api.h" "${RUNTIME_VENDOR}/include/tree_sitter/api.h"
cp "${RUNTIME_SRC}/LICENSE" "${RUNTIME_VENDOR}/LICENSE"
cp "${RUNTIME_SRC}/lib/README.md" "${RUNTIME_VENDOR}/README.upstream.md"

mkdir -p "${GRAMMAR_VENDOR}/src/tree_sitter"
rm -rf "${GRAMMAR_VENDOR}/src"
mkdir -p "${GRAMMAR_VENDOR}/src/tree_sitter"
cp "${GRAMMAR_SRC}/src/parser.c" "${GRAMMAR_VENDOR}/src/parser.c"
if [[ -f "${GRAMMAR_SRC}/src/scanner.c" ]]; then
	cp "${GRAMMAR_SRC}/src/scanner.c" "${GRAMMAR_VENDOR}/src/scanner.c"
fi
cp "${GRAMMAR_SRC}/src/tree_sitter/parser.h" "${GRAMMAR_VENDOR}/src/tree_sitter/parser.h"
cp "${GRAMMAR_SRC}/src/grammar.json" "${GRAMMAR_VENDOR}/src/grammar.json"
cp "${GRAMMAR_SRC}/src/node-types.json" "${GRAMMAR_VENDOR}/src/node-types.json"
cp "${GRAMMAR_SRC}/grammar.js" "${GRAMMAR_VENDOR}/grammar.js"
cp "${GRAMMAR_SRC}/tree-sitter.json" "${GRAMMAR_VENDOR}/tree-sitter.json"
cp "${GRAMMAR_SRC}/package.json" "${GRAMMAR_VENDOR}/package.json"
cp "${GRAMMAR_SRC}/LICENSE" "${GRAMMAR_VENDOR}/LICENSE"
cp "${GRAMMAR_SRC}/README.md" "${GRAMMAR_VENDOR}/README.upstream.md"

echo "Tree-sitter vendor refresh complete." >&2
echo "If you changed refs/releases, update vendor/tree_sitter/VERSIONS.env and VERSIONS.md." >&2

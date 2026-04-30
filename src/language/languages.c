#include "language/languages.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

#include "language/syntax_query_data.h"

extern const TSLanguage *tree_sitter_c(void);
extern const TSLanguage *tree_sitter_cpp(void);
extern const TSLanguage *tree_sitter_go(void);
extern const TSLanguage *tree_sitter_bash(void);
extern const TSLanguage *tree_sitter_html(void);
extern const TSLanguage *tree_sitter_javascript(void);
extern const TSLanguage *tree_sitter_jsdoc(void);
extern const TSLanguage *tree_sitter_typescript(void);
extern const TSLanguage *tree_sitter_css(void);
extern const TSLanguage *tree_sitter_json(void);
extern const TSLanguage *tree_sitter_python(void);
extern const TSLanguage *tree_sitter_php(void);
extern const TSLanguage *tree_sitter_rust(void);
extern const TSLanguage *tree_sitter_java(void);
extern const TSLanguage *tree_sitter_regex(void);
extern const TSLanguage *tree_sitter_c_sharp(void);
extern const TSLanguage *tree_sitter_haskell(void);
extern const TSLanguage *tree_sitter_ruby(void);
extern const TSLanguage *tree_sitter_ocaml(void);
extern const TSLanguage *tree_sitter_julia(void);
extern const TSLanguage *tree_sitter_scala(void);
extern const TSLanguage *tree_sitter_embedded_template(void);

static const TSLanguage *editorSyntaxFactoryEjs(void) {
	return tree_sitter_embedded_template();
}

static const TSLanguage *editorSyntaxFactoryErb(void) {
	return tree_sitter_embedded_template();
}

static int editorSyntaxStringEqualsNoCaseLen(const char *a, size_t a_len,
		const char *b) {
	if (a == NULL || b == NULL) {
		return 0;
	}
	size_t b_len = strlen(b);
	if (a_len != b_len) {
		return 0;
	}
	return strncasecmp(a, b, a_len) == 0;
}

static int editorSyntaxShellShebangMatch(const char *token, size_t len) {
	return editorSyntaxStringEqualsNoCaseLen(token, len, "sh") ||
			editorSyntaxStringEqualsNoCaseLen(token, len, "bash") ||
			editorSyntaxStringEqualsNoCaseLen(token, len, "zsh") ||
			editorSyntaxStringEqualsNoCaseLen(token, len, "ksh");
}

static int editorSyntaxRubyShebangMatch(const char *token, size_t len) {
	return editorSyntaxStringEqualsNoCaseLen(token, len, "ruby");
}

static int editorSyntaxPythonShebangMatch(const char *token, size_t len) {
	if (token == NULL || len < 6 || strncasecmp(token, "python", 6) != 0) {
		return 0;
	}
	for (size_t i = 6; i < len; i++) {
		char ch = token[i];
		if (!((ch >= '0' && ch <= '9') || ch == '.')) {
			return 0;
		}
	}
	return 1;
}

static int editorSyntaxPhpShebangMatch(const char *token, size_t len) {
	if (token == NULL || len < 3 || strncasecmp(token, "php", 3) != 0) {
		return 0;
	}
	for (size_t i = 3; i < len; i++) {
		char ch = token[i];
		if (!((ch >= '0' && ch <= '9') || ch == '.')) {
			return 0;
		}
	}
	return 1;
}

static const char *const k_c_extensions[] = {".c", ".h", NULL};
static const char *const k_cpp_extensions[] = {
	".cc", ".cpp", ".cxx", ".c++", ".hh", ".hpp", ".hxx", NULL};
static const char *const k_go_extensions[] = {".go", NULL};
static const char *const k_go_basenames[] = {"go.mod", "go.sum", NULL};
static const char *const k_shell_extensions[] = {".sh", ".bash", ".zsh", ".ksh", NULL};
static const char *const k_shell_basenames[] = {
	".bashrc", ".zshrc", ".profile", ".bash_profile", ".bash_login", ".kshrc", NULL};
static const char *const k_html_extensions[] = {".html", ".htm", ".xhtml", NULL};
static const char *const k_javascript_extensions[] = {".js", ".mjs", ".cjs", ".jsx", NULL};
static const char *const k_typescript_extensions[] = {".ts", ".tsx", ".cts", ".mts", NULL};
static const char *const k_css_extensions[] = {".css", ".scss", NULL};
static const char *const k_json_extensions[] = {".json", ".jsonc", NULL};
static const char *const k_python_extensions[] = {".py", ".pyi", ".pyw", NULL};
static const char *const k_php_extensions[] = {
	".php", ".phtml", ".php3", ".php4", ".php5", ".php7", ".php8", ".phps", NULL};
static const char *const k_rust_extensions[] = {".rs", NULL};
static const char *const k_java_extensions[] = {".java", NULL};
static const char *const k_regex_extensions[] = {".regex", NULL};
static const char *const k_csharp_extensions[] = {".cs", ".csx", NULL};
static const char *const k_haskell_extensions[] = {".hs", ".lhs", NULL};
static const char *const k_ruby_extensions[] = {
	".rb", ".rake", ".gemspec", ".ru", NULL};
static const char *const k_ruby_basenames[] = {
	"Rakefile", "Gemfile", "Guardfile", "Capfile", "Vagrantfile", NULL};
static const char *const k_ocaml_extensions[] = {".ml", NULL};
static const char *const k_julia_extensions[] = {".jl", NULL};
static const char *const k_scala_extensions[] = {".scala", ".sc", NULL};
static const char *const k_ejs_extensions[] = {".ejs", NULL};
static const char *const k_erb_extensions[] = {".erb", NULL};

static const char *const k_html_injection_aliases[] = {
	"html", "hamlet", "xhamlet", "shamlet", "xshamlet", "ihamlet", "hsx", NULL};
static const char *const k_javascript_injection_aliases[] = {
	"javascript", "js", "jsx", "julius", NULL};
static const char *const k_typescript_injection_aliases[] = {
	"typescript", "ts", "tsx", "tsc", "tscJSX", NULL};
static const char *const k_css_injection_aliases[] = {
	"css", "lucius", "cassius", NULL};
static const char *const k_jsdoc_injection_aliases[] = {"jsdoc", NULL};
static const char *const k_ruby_injection_aliases[] = {"ruby", "rb", NULL};
static const char *const k_json_injection_aliases[] = {"json", "aesonQQ", NULL};
static const char *const k_regex_injection_aliases[] = {"regex", "regexp", NULL};
static const char *const k_shell_injection_aliases[] = {
	"bash", "sh", "shell", NULL};

static const struct editorSyntaxLanguageDef g_languages[] = {
	{
		.id = EDITOR_SYNTAX_C,
		.name = "c",
		.ts_factory = tree_sitter_c,
		.highlight_parts = editor_query_c_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_C_HIGHLIGHT_PART_COUNT,
		.extensions = k_c_extensions
	},
	{
		.id = EDITOR_SYNTAX_CPP,
		.name = "cpp",
		.ts_factory = tree_sitter_cpp,
		.highlight_parts = editor_query_cpp_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_CPP_HIGHLIGHT_PART_COUNT,
		.injection_parts = editor_query_cpp_injection_parts,
		.injection_part_count = EDITOR_QUERY_CPP_INJECTION_PART_COUNT,
		.extensions = k_cpp_extensions
	},
	{
		.id = EDITOR_SYNTAX_GO,
		.name = "go",
		.ts_factory = tree_sitter_go,
		.highlight_parts = editor_query_go_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_GO_HIGHLIGHT_PART_COUNT,
		.extensions = k_go_extensions,
		.basenames = k_go_basenames
	},
	{
		.id = EDITOR_SYNTAX_SHELL,
		.name = "shell",
		.ts_factory = tree_sitter_bash,
		.highlight_parts = editor_query_shell_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_SHELL_HIGHLIGHT_PART_COUNT,
		.extensions = k_shell_extensions,
		.basenames = k_shell_basenames,
		.shebang_matches = editorSyntaxShellShebangMatch,
		.injection_aliases = k_shell_injection_aliases
	},
	{
		.id = EDITOR_SYNTAX_HTML,
		.name = "html",
		.ts_factory = tree_sitter_html,
		.highlight_parts = editor_query_html_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_HTML_HIGHLIGHT_PART_COUNT,
		.injection_parts = editor_query_html_injection_parts,
		.injection_part_count = EDITOR_QUERY_HTML_INJECTION_PART_COUNT,
		.extensions = k_html_extensions,
		.injection_aliases = k_html_injection_aliases
	},
	{
		.id = EDITOR_SYNTAX_JAVASCRIPT,
		.name = "javascript",
		.ts_factory = tree_sitter_javascript,
		.highlight_parts = editor_query_javascript_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_JAVASCRIPT_HIGHLIGHT_PART_COUNT,
		.locals_parts = editor_query_javascript_locals_parts,
		.locals_part_count = EDITOR_QUERY_JAVASCRIPT_LOCALS_PART_COUNT,
		.injection_parts = editor_query_javascript_injection_parts,
		.injection_part_count = EDITOR_QUERY_JAVASCRIPT_INJECTION_PART_COUNT,
		.extensions = k_javascript_extensions,
		.injection_aliases = k_javascript_injection_aliases
	},
	{
		.id = EDITOR_SYNTAX_JSDOC,
		.name = "jsdoc",
		.ts_factory = tree_sitter_jsdoc,
		.highlight_parts = editor_query_jsdoc_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_JSDOC_HIGHLIGHT_PART_COUNT,
		.injection_aliases = k_jsdoc_injection_aliases
	},
	{
		.id = EDITOR_SYNTAX_TYPESCRIPT,
		.name = "typescript",
		.ts_factory = tree_sitter_typescript,
		.highlight_parts = editor_query_typescript_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_TYPESCRIPT_HIGHLIGHT_PART_COUNT,
		.locals_parts = editor_query_typescript_locals_parts,
		.locals_part_count = EDITOR_QUERY_TYPESCRIPT_LOCALS_PART_COUNT,
		.injection_parts = editor_query_typescript_injection_parts,
		.injection_part_count = EDITOR_QUERY_TYPESCRIPT_INJECTION_PART_COUNT,
		.extensions = k_typescript_extensions,
		.injection_aliases = k_typescript_injection_aliases
	},
	{
		.id = EDITOR_SYNTAX_CSS,
		.name = "css",
		.ts_factory = tree_sitter_css,
		.highlight_parts = editor_query_css_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_CSS_HIGHLIGHT_PART_COUNT,
		.extensions = k_css_extensions,
		.injection_aliases = k_css_injection_aliases
	},
	{
		.id = EDITOR_SYNTAX_JSON,
		.name = "json",
		.ts_factory = tree_sitter_json,
		.highlight_parts = editor_query_json_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_JSON_HIGHLIGHT_PART_COUNT,
		.extensions = k_json_extensions,
		.injection_aliases = k_json_injection_aliases
	},
	{
		.id = EDITOR_SYNTAX_PYTHON,
		.name = "python",
		.ts_factory = tree_sitter_python,
		.highlight_parts = editor_query_python_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_PYTHON_HIGHLIGHT_PART_COUNT,
		.extensions = k_python_extensions,
		.shebang_matches = editorSyntaxPythonShebangMatch
	},
	{
		.id = EDITOR_SYNTAX_PHP,
		.name = "php",
		.ts_factory = tree_sitter_php,
		.highlight_parts = editor_query_php_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_PHP_HIGHLIGHT_PART_COUNT,
		.injection_parts = editor_query_php_injection_parts,
		.injection_part_count = EDITOR_QUERY_PHP_INJECTION_PART_COUNT,
		.extensions = k_php_extensions,
		.shebang_matches = editorSyntaxPhpShebangMatch
	},
	{
		.id = EDITOR_SYNTAX_RUST,
		.name = "rust",
		.ts_factory = tree_sitter_rust,
		.highlight_parts = editor_query_rust_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_RUST_HIGHLIGHT_PART_COUNT,
		.extensions = k_rust_extensions
	},
	{
		.id = EDITOR_SYNTAX_JAVA,
		.name = "java",
		.ts_factory = tree_sitter_java,
		.highlight_parts = editor_query_java_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_JAVA_HIGHLIGHT_PART_COUNT,
		.extensions = k_java_extensions
	},
	{
		.id = EDITOR_SYNTAX_REGEX,
		.name = "regex",
		.ts_factory = tree_sitter_regex,
		.highlight_parts = editor_query_regex_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_REGEX_HIGHLIGHT_PART_COUNT,
		.extensions = k_regex_extensions,
		.injection_aliases = k_regex_injection_aliases
	},
	{
		.id = EDITOR_SYNTAX_CSHARP,
		.name = "csharp",
		.ts_factory = tree_sitter_c_sharp,
		.highlight_parts = editor_query_csharp_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_CSHARP_HIGHLIGHT_PART_COUNT,
		.extensions = k_csharp_extensions
	},
	{
		.id = EDITOR_SYNTAX_HASKELL,
		.name = "haskell",
		.ts_factory = tree_sitter_haskell,
		.highlight_parts = editor_query_haskell_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_HASKELL_HIGHLIGHT_PART_COUNT,
		.locals_parts = editor_query_haskell_locals_parts,
		.locals_part_count = EDITOR_QUERY_HASKELL_LOCALS_PART_COUNT,
		.injection_parts = editor_query_haskell_injection_parts,
		.injection_part_count = EDITOR_QUERY_HASKELL_INJECTION_PART_COUNT,
		.extensions = k_haskell_extensions
	},
	{
		.id = EDITOR_SYNTAX_RUBY,
		.name = "ruby",
		.ts_factory = tree_sitter_ruby,
		.highlight_parts = editor_query_ruby_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_RUBY_HIGHLIGHT_PART_COUNT,
		.locals_parts = editor_query_ruby_locals_parts,
		.locals_part_count = EDITOR_QUERY_RUBY_LOCALS_PART_COUNT,
		.extensions = k_ruby_extensions,
		.basenames = k_ruby_basenames,
		.shebang_matches = editorSyntaxRubyShebangMatch,
		.injection_aliases = k_ruby_injection_aliases
	},
	{
		.id = EDITOR_SYNTAX_OCAML,
		.name = "ocaml",
		.ts_factory = tree_sitter_ocaml,
		.highlight_parts = editor_query_ocaml_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_OCAML_HIGHLIGHT_PART_COUNT,
		.locals_parts = editor_query_ocaml_locals_parts,
		.locals_part_count = EDITOR_QUERY_OCAML_LOCALS_PART_COUNT,
		.extensions = k_ocaml_extensions
	},
	{
		.id = EDITOR_SYNTAX_JULIA,
		.name = "julia",
		.ts_factory = tree_sitter_julia,
		.highlight_parts = editor_query_julia_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_JULIA_HIGHLIGHT_PART_COUNT,
		.locals_parts = editor_query_julia_locals_parts,
		.locals_part_count = EDITOR_QUERY_JULIA_LOCALS_PART_COUNT,
		.injection_parts = editor_query_julia_injection_parts,
		.injection_part_count = EDITOR_QUERY_JULIA_INJECTION_PART_COUNT,
		.extensions = k_julia_extensions
	},
	{
		.id = EDITOR_SYNTAX_SCALA,
		.name = "scala",
		.ts_factory = tree_sitter_scala,
		.highlight_parts = editor_query_scala_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_SCALA_HIGHLIGHT_PART_COUNT,
		.locals_parts = editor_query_scala_locals_parts,
		.locals_part_count = EDITOR_QUERY_SCALA_LOCALS_PART_COUNT,
		.extensions = k_scala_extensions
	},
	{
		.id = EDITOR_SYNTAX_EJS,
		.name = "ejs",
		.ts_factory = editorSyntaxFactoryEjs,
		.highlight_parts = editor_query_ejs_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_EJS_HIGHLIGHT_PART_COUNT,
		.injection_parts = editor_query_ejs_injection_parts,
		.injection_part_count = EDITOR_QUERY_EJS_INJECTION_PART_COUNT,
		.extensions = k_ejs_extensions
	},
	{
		.id = EDITOR_SYNTAX_ERB,
		.name = "erb",
		.ts_factory = editorSyntaxFactoryErb,
		.highlight_parts = editor_query_erb_highlight_parts,
		.highlight_part_count = EDITOR_QUERY_ERB_HIGHLIGHT_PART_COUNT,
		.injection_parts = editor_query_erb_injection_parts,
		.injection_part_count = EDITOR_QUERY_ERB_INJECTION_PART_COUNT,
		.extensions = k_erb_extensions
	}
};

#define ROTIDE_LANGUAGE_DEF_COUNT \
	((int)(sizeof(g_languages) / sizeof(g_languages[0])))

int editorSyntaxLanguageDefCount(void) {
	return ROTIDE_LANGUAGE_DEF_COUNT;
}

const struct editorSyntaxLanguageDef *editorSyntaxLanguageDefAt(int idx) {
	if (idx < 0 || idx >= ROTIDE_LANGUAGE_DEF_COUNT) {
		return NULL;
	}
	return &g_languages[idx];
}

const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguage(enum editorSyntaxLanguage id) {
	if (id == EDITOR_SYNTAX_NONE) {
		return NULL;
	}
	for (int i = 0; i < ROTIDE_LANGUAGE_DEF_COUNT; i++) {
		if (g_languages[i].id == id) {
			return &g_languages[i];
		}
	}
	return NULL;
}

const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguageByName(const char *name,
		size_t len) {
	if (name == NULL) {
		return NULL;
	}
	for (int i = 0; i < ROTIDE_LANGUAGE_DEF_COUNT; i++) {
		const struct editorSyntaxLanguageDef *def = &g_languages[i];
		if (editorSyntaxStringEqualsNoCaseLen(name, len, def->name)) {
			return def;
		}
	}
	return NULL;
}

const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguageByExtension(const char *ext) {
	if (ext == NULL) {
		return NULL;
	}
	for (int i = 0; i < ROTIDE_LANGUAGE_DEF_COUNT; i++) {
		const struct editorSyntaxLanguageDef *def = &g_languages[i];
		if (def->extensions == NULL) {
			continue;
		}
		for (const char *const *p = def->extensions; *p != NULL; p++) {
			if (strcmp(*p, ext) == 0) {
				return def;
			}
		}
	}
	return NULL;
}

const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguageByBasename(const char *base) {
	if (base == NULL) {
		return NULL;
	}
	for (int i = 0; i < ROTIDE_LANGUAGE_DEF_COUNT; i++) {
		const struct editorSyntaxLanguageDef *def = &g_languages[i];
		if (def->basenames == NULL) {
			continue;
		}
		for (const char *const *p = def->basenames; *p != NULL; p++) {
			if (strcmp(*p, base) == 0) {
				return def;
			}
		}
	}
	return NULL;
}

const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguageByShebangToken(
		const char *token, size_t len) {
	if (token == NULL || len == 0) {
		return NULL;
	}
	for (int i = 0; i < ROTIDE_LANGUAGE_DEF_COUNT; i++) {
		const struct editorSyntaxLanguageDef *def = &g_languages[i];
		if (def->shebang_matches == NULL) {
			continue;
		}
		if (def->shebang_matches(token, len)) {
			return def;
		}
	}
	return NULL;
}

const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguageByInjectionName(
		const char *name, size_t len) {
	if (name == NULL) {
		return NULL;
	}
	while (len > 0 && isspace((unsigned char)*name)) {
		name++;
		len--;
	}
	while (len > 0 &&
			(isspace((unsigned char)name[len - 1]) || name[len - 1] == ';')) {
		len--;
	}
	if (len >= 2 &&
			((name[0] == '"' && name[len - 1] == '"') ||
				(name[0] == '\'' && name[len - 1] == '\''))) {
		name++;
		len -= 2;
	}
	if (len == 0) {
		return NULL;
	}

	const struct editorSyntaxLanguageDef *by_name =
			editorSyntaxLookupLanguageByName(name, len);
	if (by_name != NULL) {
		return by_name;
	}

	for (int i = 0; i < ROTIDE_LANGUAGE_DEF_COUNT; i++) {
		const struct editorSyntaxLanguageDef *def = &g_languages[i];
		if (def->injection_aliases == NULL) {
			continue;
		}
		for (const char *const *p = def->injection_aliases; *p != NULL; p++) {
			if (editorSyntaxStringEqualsNoCaseLen(name, len, *p)) {
				return def;
			}
		}
	}
	return NULL;
}

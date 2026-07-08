#include "language/languages.h"

#include "language/syntax.h"
#include "language/syntax_query_data.h"
#include "tree_sitter/api.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

extern const TSLanguage *tree_sitter_c(void);
extern const TSLanguage *tree_sitter_cpp(void);
extern const TSLanguage *tree_sitter_go(void);
extern const TSLanguage *tree_sitter_bash(void);
extern const TSLanguage *tree_sitter_html(void);
extern const TSLanguage *tree_sitter_javascript(void);
extern const TSLanguage *tree_sitter_jsdoc(void);
extern const TSLanguage *tree_sitter_typescript(void);
extern const TSLanguage *tree_sitter_tsx(void);
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
extern const TSLanguage *tree_sitter_markdown(void);
extern const TSLanguage *tree_sitter_markdown_inline(void);
extern const TSLanguage *tree_sitter_toml(void);
extern const TSLanguage *tree_sitter_yaml(void);
extern const TSLanguage *tree_sitter_xml(void);
extern const TSLanguage *tree_sitter_make(void);
extern const TSLanguage *tree_sitter_diff(void);
extern const TSLanguage *tree_sitter_latex(void);
extern const TSLanguage *tree_sitter_bibtex(void);
extern const TSLanguage *tree_sitter_hcl(void);
extern const TSLanguage *tree_sitter_lua(void);
extern const TSLanguage *tree_sitter_glsl(void);
extern const TSLanguage *tree_sitter_kotlin(void);
extern const TSLanguage *tree_sitter_svelte(void);
extern const TSLanguage *tree_sitter_vue(void);
extern const TSLanguage *tree_sitter_helm(void);
extern const TSLanguage *tree_sitter_containerfile(void);
extern const TSLanguage *tree_sitter_clojure(void);
extern const TSLanguage *tree_sitter_r(void);
extern const TSLanguage *tree_sitter_gdscript(void);
extern const TSLanguage *tree_sitter_zig(void);
extern const TSLanguage *tree_sitter_swift(void);
extern const TSLanguage *tree_sitter_perl(void);

static const TSLanguage *languagesSyntaxFactoryEjs(void) {
	return tree_sitter_embedded_template();
}

static const TSLanguage *languagesSyntaxFactoryErb(void) {
	return tree_sitter_embedded_template();
}

static int languagesStringEqualsNoCaseLen(const char *a, size_t a_len, const char *b) {
	if (a == NULL || b == NULL) {
		return 0;
	}
	size_t b_len = strlen(b);
	if (a_len != b_len) {
		return 0;
	}
	return strncasecmp(a, b, a_len) == 0;
}

static int languagesShellShebangMatch(const char *token, size_t len) {
	return languagesStringEqualsNoCaseLen(token, len, "sh") ||
	       languagesStringEqualsNoCaseLen(token, len, "bash") ||
	       languagesStringEqualsNoCaseLen(token, len, "zsh") ||
	       languagesStringEqualsNoCaseLen(token, len, "ksh");
}

static int languagesRubyShebangMatch(const char *token, size_t len) {
	return languagesStringEqualsNoCaseLen(token, len, "ruby");
}

static int languagesLuaShebangMatch(const char *token, size_t len) {
	return languagesStringEqualsNoCaseLen(token, len, "lua") ||
	       languagesStringEqualsNoCaseLen(token, len, "luajit");
}

static int languagesRShebangMatch(const char *token, size_t len) {
	return languagesStringEqualsNoCaseLen(token, len, "Rscript");
}

static int languagesPerlShebangMatch(const char *token, size_t len) {
	return languagesStringEqualsNoCaseLen(token, len, "perl");
}

static int languagesPythonShebangMatch(const char *token, size_t len) {
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

static int languagesPhpShebangMatch(const char *token, size_t len) {
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
static const char *const k_cpp_extensions[] = {".cc", ".cpp", ".cxx", ".c++",
                                               ".hh", ".hpp", ".hxx", NULL};
static const char *const k_go_extensions[] = {".go", NULL};
static const char *const k_go_basenames[] = {"go.mod", "go.sum", NULL};
static const char *const k_shell_extensions[] = {".sh", ".bash", ".zsh", ".ksh", NULL};
static const char *const k_shell_basenames[] = {
        ".bashrc", ".zshrc", ".profile", ".bash_profile", ".bash_login", ".kshrc", NULL};
static const char *const k_html_extensions[] = {".html", ".htm", ".xhtml", NULL};
static const char *const k_javascript_extensions[] = {".js", ".mjs", ".cjs", ".jsx", NULL};
static const char *const k_typescript_extensions[] = {".ts", ".cts", ".mts", NULL};
static const char *const k_tsx_extensions[] = {".tsx", NULL};
static const char *const k_css_extensions[] = {".css", ".scss", NULL};
static const char *const k_json_extensions[] = {".json", ".jsonc", NULL};
static const char *const k_python_extensions[] = {".py", ".pyi", ".pyw", NULL};
static const char *const k_php_extensions[] = {".php",  ".phtml", ".php3", ".php4", ".php5",
                                               ".php7", ".php8",  ".phps", NULL};
static const char *const k_rust_extensions[] = {".rs", NULL};
static const char *const k_java_extensions[] = {".java", NULL};
static const char *const k_regex_extensions[] = {".regex", NULL};
static const char *const k_csharp_extensions[] = {".cs", ".csx", NULL};
static const char *const k_haskell_extensions[] = {".hs", ".lhs", NULL};
static const char *const k_ruby_extensions[] = {".rb", ".rake", ".gemspec", ".ru", NULL};
static const char *const k_ruby_basenames[] = {"Rakefile", "Gemfile",     "Guardfile",
                                               "Capfile",  "Vagrantfile", NULL};
static const char *const k_ocaml_extensions[] = {".ml", NULL};
static const char *const k_julia_extensions[] = {".jl", NULL};
static const char *const k_scala_extensions[] = {".scala", ".sc", NULL};
static const char *const k_ejs_extensions[] = {".ejs", NULL};
static const char *const k_erb_extensions[] = {".erb", NULL};
static const char *const k_markdown_extensions[] = {".md", ".markdown", NULL};
static const char *const k_toml_extensions[] = {".toml", ".toml.example", NULL};
static const char *const k_yaml_extensions[] = {".yaml", ".yml", ".yaml.example", ".yml.example",
                                                NULL};
static const char *const k_xml_extensions[] = {".xml", ".svg", ".xsd", ".xslt",
                                               ".xsl", ".rng", NULL};
static const char *const k_make_extensions[] = {".mk", ".mak", NULL};
static const char *const k_make_basenames[] = {"Makefile", "makefile", "GNUmakefile", "BSDmakefile",
                                               NULL};
static const char *const k_diff_extensions[] = {".diff", ".patch", NULL};
static const char *const k_latex_extensions[] = {".tex", ".ltx", ".sty", ".cls",
                                                 ".dtx", ".ins", NULL};
static const char *const k_bibtex_extensions[] = {".bib", NULL};
static const char *const k_hcl_extensions[] = {".hcl", ".tf", ".tfvars", ".nomad", NULL};
static const char *const k_lua_extensions[] = {".lua", NULL};
static const char *const k_glsl_extensions[] = {
        ".glsl", ".vert", ".frag",  ".geom",  ".comp",  ".tesc", ".tese",  ".mesh",
        ".task", ".rgen", ".rchit", ".rahit", ".rmiss", ".rint", ".rcall", NULL};
static const char *const k_kotlin_extensions[] = {".kt", ".kts", ".ktm", NULL};
static const char *const k_svelte_extensions[] = {".svelte", NULL};
static const char *const k_vue_extensions[] = {".vue", NULL};
static const char *const k_helm_extensions[] = {".tpl", ".gotmpl", ".helm", NULL};
static const char *const k_dockerfile_extensions[] = {".dockerfile", ".containerfile", NULL};
static const char *const k_dockerfile_basenames[] = {"Dockerfile", "dockerfile", "Containerfile",
                                                     "containerfile", NULL};
static const char *const k_clojure_extensions[] = {".clj", ".cljs", ".cljc", ".cljd",
                                                   ".edn", ".bb",   ".boot", NULL};
static const char *const k_r_extensions[] = {".R", ".r", NULL};
static const char *const k_r_basenames[] = {".Rprofile", ".Rhistory", NULL};
static const char *const k_gdscript_extensions[] = {".gd", NULL};
static const char *const k_zig_extensions[] = {".zig", NULL};
static const char *const k_swift_extensions[] = {".swift", NULL};
static const char *const k_perl_extensions[] = {".pl", ".pm", ".t", NULL};

static const char *const k_html_injection_aliases[] = {"html",     "hamlet",  "xhamlet", "shamlet",
                                                       "xshamlet", "ihamlet", "hsx",     NULL};
static const char *const k_javascript_injection_aliases[] = {"javascript", "js", "jsx", "julius",
                                                             NULL};
static const char *const k_typescript_injection_aliases[] = {"typescript", "ts", "tsc", NULL};
static const char *const k_tsx_injection_aliases[] = {"tsx", "tscJSX", NULL};
static const char *const k_css_injection_aliases[] = {"css", "lucius", "cassius", NULL};
static const char *const k_jsdoc_injection_aliases[] = {"jsdoc", NULL};
static const char *const k_ruby_injection_aliases[] = {"ruby", "rb", NULL};
static const char *const k_json_injection_aliases[] = {"json", "aesonQQ", NULL};
static const char *const k_regex_injection_aliases[] = {"regex", "regexp", NULL};
static const char *const k_shell_injection_aliases[] = {"bash", "sh", "shell", NULL};
static const char *const k_markdown_injection_aliases[] = {"markdown", "md", NULL};
static const char *const k_markdown_inline_injection_aliases[] = {"markdown_inline",
                                                                  "markdown.inline", NULL};
static const char *const k_toml_injection_aliases[] = {"toml", NULL};
static const char *const k_yaml_injection_aliases[] = {"yaml", "yml", NULL};
static const char *const k_xml_injection_aliases[] = {"xml", "svg", "xsd", "xslt",
                                                      "xsl", "rng", NULL};
static const char *const k_make_injection_aliases[] = {"make", "makefile", "gnumake", NULL};
static const char *const k_diff_injection_aliases[] = {"diff", "patch", NULL};
static const char *const k_latex_injection_aliases[] = {"latex", "tex", NULL};
static const char *const k_bibtex_injection_aliases[] = {"bibtex", "bib", NULL};
static const char *const k_hcl_injection_aliases[] = {"hcl", "terraform", "tf", NULL};
static const char *const k_lua_injection_aliases[] = {"lua", "luajit", NULL};
static const char *const k_glsl_injection_aliases[] = {"glsl", "vert", "frag", NULL};
static const char *const k_kotlin_injection_aliases[] = {"kotlin", "kt", NULL};
static const char *const k_svelte_injection_aliases[] = {"svelte", NULL};
static const char *const k_vue_injection_aliases[] = {"vue", NULL};
static const char *const k_helm_injection_aliases[] = {"helm", "gotmpl", "go-template", NULL};
static const char *const k_dockerfile_injection_aliases[] = {"dockerfile", "containerfile",
                                                             "docker", "container", NULL};
static const char *const k_clojure_injection_aliases[] = {"clojure", "clj", "cljs", "cljc",
                                                          "cljd",    "edn", NULL};
static const char *const k_r_injection_aliases[] = {"r", NULL};
static const char *const k_gdscript_injection_aliases[] = {"gdscript", "gd", NULL};
static const char *const k_zig_injection_aliases[] = {"zig", NULL};
static const char *const k_swift_injection_aliases[] = {"swift", NULL};
static const char *const k_perl_injection_aliases[] = {"perl", "pl", NULL};

static const struct editorSyntaxLanguageDef g_languages[] = {
        {.id = EDITOR_SYNTAX_C,
         .name = "c",
         .ts_factory = tree_sitter_c,
         .highlight_parts = editor_query_c_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_C_HIGHLIGHT_PART_COUNT,
         .extensions = k_c_extensions},
        {.id = EDITOR_SYNTAX_CPP,
         .name = "cpp",
         .ts_factory = tree_sitter_cpp,
         .highlight_parts = editor_query_cpp_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_CPP_HIGHLIGHT_PART_COUNT,
         .injection_parts = editor_query_cpp_injection_parts,
         .injection_part_count = EDITOR_QUERY_CPP_INJECTION_PART_COUNT,
         .extensions = k_cpp_extensions},
        {.id = EDITOR_SYNTAX_GO,
         .name = "go",
         .ts_factory = tree_sitter_go,
         .highlight_parts = editor_query_go_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_GO_HIGHLIGHT_PART_COUNT,
         .extensions = k_go_extensions,
         .basenames = k_go_basenames},
        {.id = EDITOR_SYNTAX_SHELL,
         .name = "shell",
         .ts_factory = tree_sitter_bash,
         .highlight_parts = editor_query_shell_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_SHELL_HIGHLIGHT_PART_COUNT,
         .extensions = k_shell_extensions,
         .basenames = k_shell_basenames,
         .shebang_matches = languagesShellShebangMatch,
         .injection_aliases = k_shell_injection_aliases},
        {.id = EDITOR_SYNTAX_HTML,
         .name = "html",
         .ts_factory = tree_sitter_html,
         .highlight_parts = editor_query_html_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_HTML_HIGHLIGHT_PART_COUNT,
         .injection_parts = editor_query_html_injection_parts,
         .injection_part_count = EDITOR_QUERY_HTML_INJECTION_PART_COUNT,
         .extensions = k_html_extensions,
         .injection_aliases = k_html_injection_aliases},
        {.id = EDITOR_SYNTAX_JAVASCRIPT,
         .name = "javascript",
         .ts_factory = tree_sitter_javascript,
         .highlight_parts = editor_query_javascript_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_JAVASCRIPT_HIGHLIGHT_PART_COUNT,
         .locals_parts = editor_query_javascript_locals_parts,
         .locals_part_count = EDITOR_QUERY_JAVASCRIPT_LOCALS_PART_COUNT,
         .injection_parts = editor_query_javascript_injection_parts,
         .injection_part_count = EDITOR_QUERY_JAVASCRIPT_INJECTION_PART_COUNT,
         .extensions = k_javascript_extensions,
         .injection_aliases = k_javascript_injection_aliases},
        {.id = EDITOR_SYNTAX_JSDOC,
         .name = "jsdoc",
         .ts_factory = tree_sitter_jsdoc,
         .highlight_parts = editor_query_jsdoc_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_JSDOC_HIGHLIGHT_PART_COUNT,
         .injection_aliases = k_jsdoc_injection_aliases},
        {.id = EDITOR_SYNTAX_TYPESCRIPT,
         .name = "typescript",
         .ts_factory = tree_sitter_typescript,
         .highlight_parts = editor_query_typescript_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_TYPESCRIPT_HIGHLIGHT_PART_COUNT,
         .locals_parts = editor_query_typescript_locals_parts,
         .locals_part_count = EDITOR_QUERY_TYPESCRIPT_LOCALS_PART_COUNT,
         .injection_parts = editor_query_typescript_injection_parts,
         .injection_part_count = EDITOR_QUERY_TYPESCRIPT_INJECTION_PART_COUNT,
         .extensions = k_typescript_extensions,
         .injection_aliases = k_typescript_injection_aliases},
        {.id = EDITOR_SYNTAX_TSX,
         .name = "tsx",
         .ts_factory = tree_sitter_tsx,
         .highlight_parts = editor_query_tsx_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_TSX_HIGHLIGHT_PART_COUNT,
         .locals_parts = editor_query_tsx_locals_parts,
         .locals_part_count = EDITOR_QUERY_TSX_LOCALS_PART_COUNT,
         .injection_parts = editor_query_tsx_injection_parts,
         .injection_part_count = EDITOR_QUERY_TSX_INJECTION_PART_COUNT,
         .extensions = k_tsx_extensions,
         .injection_aliases = k_tsx_injection_aliases},
        {.id = EDITOR_SYNTAX_CSS,
         .name = "css",
         .ts_factory = tree_sitter_css,
         .highlight_parts = editor_query_css_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_CSS_HIGHLIGHT_PART_COUNT,
         .extensions = k_css_extensions,
         .injection_aliases = k_css_injection_aliases},
        {.id = EDITOR_SYNTAX_JSON,
         .name = "json",
         .ts_factory = tree_sitter_json,
         .highlight_parts = editor_query_json_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_JSON_HIGHLIGHT_PART_COUNT,
         .extensions = k_json_extensions,
         .injection_aliases = k_json_injection_aliases},
        {.id = EDITOR_SYNTAX_PYTHON,
         .name = "python",
         .ts_factory = tree_sitter_python,
         .highlight_parts = editor_query_python_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_PYTHON_HIGHLIGHT_PART_COUNT,
         .extensions = k_python_extensions,
         .shebang_matches = languagesPythonShebangMatch},
        {.id = EDITOR_SYNTAX_PHP,
         .name = "php",
         .ts_factory = tree_sitter_php,
         .highlight_parts = editor_query_php_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_PHP_HIGHLIGHT_PART_COUNT,
         .injection_parts = editor_query_php_injection_parts,
         .injection_part_count = EDITOR_QUERY_PHP_INJECTION_PART_COUNT,
         .extensions = k_php_extensions,
         .shebang_matches = languagesPhpShebangMatch},
        {.id = EDITOR_SYNTAX_RUST,
         .name = "rust",
         .ts_factory = tree_sitter_rust,
         .highlight_parts = editor_query_rust_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_RUST_HIGHLIGHT_PART_COUNT,
         .extensions = k_rust_extensions},
        {.id = EDITOR_SYNTAX_JAVA,
         .name = "java",
         .ts_factory = tree_sitter_java,
         .highlight_parts = editor_query_java_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_JAVA_HIGHLIGHT_PART_COUNT,
         .extensions = k_java_extensions},
        {.id = EDITOR_SYNTAX_REGEX,
         .name = "regex",
         .ts_factory = tree_sitter_regex,
         .highlight_parts = editor_query_regex_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_REGEX_HIGHLIGHT_PART_COUNT,
         .extensions = k_regex_extensions,
         .injection_aliases = k_regex_injection_aliases},
        {.id = EDITOR_SYNTAX_CSHARP,
         .name = "csharp",
         .ts_factory = tree_sitter_c_sharp,
         .highlight_parts = editor_query_csharp_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_CSHARP_HIGHLIGHT_PART_COUNT,
         .extensions = k_csharp_extensions},
        {.id = EDITOR_SYNTAX_HASKELL,
         .name = "haskell",
         .ts_factory = tree_sitter_haskell,
         .highlight_parts = editor_query_haskell_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_HASKELL_HIGHLIGHT_PART_COUNT,
         .locals_parts = editor_query_haskell_locals_parts,
         .locals_part_count = EDITOR_QUERY_HASKELL_LOCALS_PART_COUNT,
         .injection_parts = editor_query_haskell_injection_parts,
         .injection_part_count = EDITOR_QUERY_HASKELL_INJECTION_PART_COUNT,
         .extensions = k_haskell_extensions},
        {.id = EDITOR_SYNTAX_RUBY,
         .name = "ruby",
         .ts_factory = tree_sitter_ruby,
         .highlight_parts = editor_query_ruby_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_RUBY_HIGHLIGHT_PART_COUNT,
         .locals_parts = editor_query_ruby_locals_parts,
         .locals_part_count = EDITOR_QUERY_RUBY_LOCALS_PART_COUNT,
         .extensions = k_ruby_extensions,
         .basenames = k_ruby_basenames,
         .shebang_matches = languagesRubyShebangMatch,
         .injection_aliases = k_ruby_injection_aliases},
        {.id = EDITOR_SYNTAX_OCAML,
         .name = "ocaml",
         .ts_factory = tree_sitter_ocaml,
         .highlight_parts = editor_query_ocaml_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_OCAML_HIGHLIGHT_PART_COUNT,
         .locals_parts = editor_query_ocaml_locals_parts,
         .locals_part_count = EDITOR_QUERY_OCAML_LOCALS_PART_COUNT,
         .extensions = k_ocaml_extensions},
        {.id = EDITOR_SYNTAX_JULIA,
         .name = "julia",
         .ts_factory = tree_sitter_julia,
         .highlight_parts = editor_query_julia_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_JULIA_HIGHLIGHT_PART_COUNT,
         .locals_parts = editor_query_julia_locals_parts,
         .locals_part_count = EDITOR_QUERY_JULIA_LOCALS_PART_COUNT,
         .injection_parts = editor_query_julia_injection_parts,
         .injection_part_count = EDITOR_QUERY_JULIA_INJECTION_PART_COUNT,
         .extensions = k_julia_extensions},
        {.id = EDITOR_SYNTAX_SCALA,
         .name = "scala",
         .ts_factory = tree_sitter_scala,
         .highlight_parts = editor_query_scala_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_SCALA_HIGHLIGHT_PART_COUNT,
         .locals_parts = editor_query_scala_locals_parts,
         .locals_part_count = EDITOR_QUERY_SCALA_LOCALS_PART_COUNT,
         .extensions = k_scala_extensions},
        {.id = EDITOR_SYNTAX_EJS,
         .name = "ejs",
         .ts_factory = languagesSyntaxFactoryEjs,
         .highlight_parts = editor_query_ejs_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_EJS_HIGHLIGHT_PART_COUNT,
         .injection_parts = editor_query_ejs_injection_parts,
         .injection_part_count = EDITOR_QUERY_EJS_INJECTION_PART_COUNT,
         .extensions = k_ejs_extensions},
        {.id = EDITOR_SYNTAX_ERB,
         .name = "erb",
         .ts_factory = languagesSyntaxFactoryErb,
         .highlight_parts = editor_query_erb_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_ERB_HIGHLIGHT_PART_COUNT,
         .injection_parts = editor_query_erb_injection_parts,
         .injection_part_count = EDITOR_QUERY_ERB_INJECTION_PART_COUNT,
         .extensions = k_erb_extensions},
        {.id = EDITOR_SYNTAX_MARKDOWN,
         .name = "markdown",
         .ts_factory = tree_sitter_markdown,
         .highlight_parts = editor_query_markdown_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_MARKDOWN_HIGHLIGHT_PART_COUNT,
         .injection_parts = editor_query_markdown_injection_parts,
         .injection_part_count = EDITOR_QUERY_MARKDOWN_INJECTION_PART_COUNT,
         .extensions = k_markdown_extensions,
         .injection_aliases = k_markdown_injection_aliases},
        {/* Injection-only target: tree-sitter-markdown's block grammar emits
          * (inline) nodes that are reparsed with markdown_inline via the static
          * injection.language set in markdown's injections.scm. There is no
          * file detection for this id. */
         .id = EDITOR_SYNTAX_MARKDOWN_INLINE,
         .name = "markdown_inline",
         .ts_factory = tree_sitter_markdown_inline,
         .highlight_parts = editor_query_markdown_inline_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_MARKDOWN_INLINE_HIGHLIGHT_PART_COUNT,
         .injection_aliases = k_markdown_inline_injection_aliases},
        {.id = EDITOR_SYNTAX_TOML,
         .name = "toml",
         .ts_factory = tree_sitter_toml,
         .highlight_parts = editor_query_toml_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_TOML_HIGHLIGHT_PART_COUNT,
         .extensions = k_toml_extensions,
         .injection_aliases = k_toml_injection_aliases},
        {.id = EDITOR_SYNTAX_YAML,
         .name = "yaml",
         .ts_factory = tree_sitter_yaml,
         .highlight_parts = editor_query_yaml_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_YAML_HIGHLIGHT_PART_COUNT,
         .extensions = k_yaml_extensions,
         .injection_aliases = k_yaml_injection_aliases},
        {.id = EDITOR_SYNTAX_XML,
         .name = "xml",
         .ts_factory = tree_sitter_xml,
         .highlight_parts = editor_query_xml_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_XML_HIGHLIGHT_PART_COUNT,
         .extensions = k_xml_extensions,
         .injection_aliases = k_xml_injection_aliases},
        {.id = EDITOR_SYNTAX_MAKE,
         .name = "make",
         .ts_factory = tree_sitter_make,
         .highlight_parts = editor_query_make_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_MAKE_HIGHLIGHT_PART_COUNT,
         .extensions = k_make_extensions,
         .basenames = k_make_basenames,
         .injection_aliases = k_make_injection_aliases},
        {.id = EDITOR_SYNTAX_DIFF,
         .name = "diff",
         .ts_factory = tree_sitter_diff,
         .highlight_parts = editor_query_diff_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_DIFF_HIGHLIGHT_PART_COUNT,
         .extensions = k_diff_extensions,
         .injection_aliases = k_diff_injection_aliases},
        {.id = EDITOR_SYNTAX_LATEX,
         .name = "latex",
         .ts_factory = tree_sitter_latex,
         .highlight_parts = editor_query_latex_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_LATEX_HIGHLIGHT_PART_COUNT,
         .extensions = k_latex_extensions,
         .injection_aliases = k_latex_injection_aliases},
        {.id = EDITOR_SYNTAX_BIBTEX,
         .name = "bibtex",
         .ts_factory = tree_sitter_bibtex,
         .highlight_parts = editor_query_bibtex_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_BIBTEX_HIGHLIGHT_PART_COUNT,
         .extensions = k_bibtex_extensions,
         .injection_aliases = k_bibtex_injection_aliases},
        {.id = EDITOR_SYNTAX_HCL,
         .name = "hcl",
         .ts_factory = tree_sitter_hcl,
         .highlight_parts = editor_query_hcl_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_HCL_HIGHLIGHT_PART_COUNT,
         .extensions = k_hcl_extensions,
         .injection_aliases = k_hcl_injection_aliases},
        {.id = EDITOR_SYNTAX_LUA,
         .name = "lua",
         .ts_factory = tree_sitter_lua,
         .highlight_parts = editor_query_lua_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_LUA_HIGHLIGHT_PART_COUNT,
         .locals_parts = editor_query_lua_locals_parts,
         .locals_part_count = EDITOR_QUERY_LUA_LOCALS_PART_COUNT,
         .injection_parts = editor_query_lua_injection_parts,
         .injection_part_count = EDITOR_QUERY_LUA_INJECTION_PART_COUNT,
         .extensions = k_lua_extensions,
         .shebang_matches = languagesLuaShebangMatch,
         .injection_aliases = k_lua_injection_aliases},
        {.id = EDITOR_SYNTAX_GLSL,
         .name = "glsl",
         .ts_factory = tree_sitter_glsl,
         .highlight_parts = editor_query_glsl_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_GLSL_HIGHLIGHT_PART_COUNT,
         .extensions = k_glsl_extensions,
         .injection_aliases = k_glsl_injection_aliases},
        {.id = EDITOR_SYNTAX_KOTLIN,
         .name = "kotlin",
         .ts_factory = tree_sitter_kotlin,
         .highlight_parts = editor_query_kotlin_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_KOTLIN_HIGHLIGHT_PART_COUNT,
         .extensions = k_kotlin_extensions,
         .injection_aliases = k_kotlin_injection_aliases},
        {.id = EDITOR_SYNTAX_SVELTE,
         .name = "svelte",
         .ts_factory = tree_sitter_svelte,
         .highlight_parts = editor_query_svelte_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_SVELTE_HIGHLIGHT_PART_COUNT,
         .injection_parts = editor_query_svelte_injection_parts,
         .injection_part_count = EDITOR_QUERY_SVELTE_INJECTION_PART_COUNT,
         .extensions = k_svelte_extensions,
         .injection_aliases = k_svelte_injection_aliases},
        {.id = EDITOR_SYNTAX_VUE,
         .name = "vue",
         .ts_factory = tree_sitter_vue,
         .highlight_parts = editor_query_vue_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_VUE_HIGHLIGHT_PART_COUNT,
         .injection_parts = editor_query_vue_injection_parts,
         .injection_part_count = EDITOR_QUERY_VUE_INJECTION_PART_COUNT,
         .extensions = k_vue_extensions,
         .injection_aliases = k_vue_injection_aliases},
        {.id = EDITOR_SYNTAX_HELM,
         .name = "helm",
         .ts_factory = tree_sitter_helm,
         .highlight_parts = editor_query_helm_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_HELM_HIGHLIGHT_PART_COUNT,
         .injection_parts = editor_query_helm_injection_parts,
         .injection_part_count = EDITOR_QUERY_HELM_INJECTION_PART_COUNT,
         .extensions = k_helm_extensions,
         .injection_aliases = k_helm_injection_aliases},
        {.id = EDITOR_SYNTAX_DOCKERFILE,
         .name = "dockerfile",
         .ts_factory = tree_sitter_containerfile,
         .highlight_parts = editor_query_dockerfile_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_DOCKERFILE_HIGHLIGHT_PART_COUNT,
         .injection_parts = editor_query_dockerfile_injection_parts,
         .injection_part_count = EDITOR_QUERY_DOCKERFILE_INJECTION_PART_COUNT,
         .extensions = k_dockerfile_extensions,
         .basenames = k_dockerfile_basenames,
         .injection_aliases = k_dockerfile_injection_aliases},
        {.id = EDITOR_SYNTAX_CLOJURE,
         .name = "clojure",
         .ts_factory = tree_sitter_clojure,
         .highlight_parts = editor_query_clojure_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_CLOJURE_HIGHLIGHT_PART_COUNT,
         .extensions = k_clojure_extensions,
         .injection_aliases = k_clojure_injection_aliases},
        {.id = EDITOR_SYNTAX_R,
         .name = "r",
         .ts_factory = tree_sitter_r,
         .highlight_parts = editor_query_r_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_R_HIGHLIGHT_PART_COUNT,
         .locals_parts = editor_query_r_locals_parts,
         .locals_part_count = EDITOR_QUERY_R_LOCALS_PART_COUNT,
         .extensions = k_r_extensions,
         .basenames = k_r_basenames,
         .shebang_matches = languagesRShebangMatch,
         .injection_aliases = k_r_injection_aliases},
        {.id = EDITOR_SYNTAX_GDSCRIPT,
         .name = "gdscript",
         .ts_factory = tree_sitter_gdscript,
         .highlight_parts = editor_query_gdscript_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_GDSCRIPT_HIGHLIGHT_PART_COUNT,
         .extensions = k_gdscript_extensions,
         .injection_aliases = k_gdscript_injection_aliases},
        {.id = EDITOR_SYNTAX_ZIG,
         .name = "zig",
         .ts_factory = tree_sitter_zig,
         .highlight_parts = editor_query_zig_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_ZIG_HIGHLIGHT_PART_COUNT,
         .extensions = k_zig_extensions,
         .injection_aliases = k_zig_injection_aliases},
        {.id = EDITOR_SYNTAX_SWIFT,
         .name = "swift",
         .ts_factory = tree_sitter_swift,
         .highlight_parts = editor_query_swift_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_SWIFT_HIGHLIGHT_PART_COUNT,
         .injection_parts = editor_query_swift_injection_parts,
         .injection_part_count = EDITOR_QUERY_SWIFT_INJECTION_PART_COUNT,
         .extensions = k_swift_extensions,
         .injection_aliases = k_swift_injection_aliases},
        {.id = EDITOR_SYNTAX_PERL,
         .name = "perl",
         .ts_factory = tree_sitter_perl,
         .highlight_parts = editor_query_perl_highlight_parts,
         .highlight_part_count = EDITOR_QUERY_PERL_HIGHLIGHT_PART_COUNT,
         .injection_parts = editor_query_perl_injection_parts,
         .injection_part_count = EDITOR_QUERY_PERL_INJECTION_PART_COUNT,
         .extensions = k_perl_extensions,
         .shebang_matches = languagesPerlShebangMatch,
         .injection_aliases = k_perl_injection_aliases}};

#define ROTIDE_LANGUAGE_DEF_COUNT ((int)(sizeof(g_languages) / sizeof(g_languages[0])))

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
		if (languagesStringEqualsNoCaseLen(name, len, def->name)) {
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

const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguageByShebangToken(const char *token,
                                                                               size_t len) {
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

const struct editorSyntaxLanguageDef *editorSyntaxLookupLanguageByInjectionName(const char *name,
                                                                                size_t len) {
	if (name == NULL) {
		return NULL;
	}
	while (len > 0 && isspace((unsigned char)*name)) {
		name++;
		len--;
	}
	while (len > 0 && (isspace((unsigned char)name[len - 1]) || name[len - 1] == ';')) {
		len--;
	}
	if (len >= 2 && ((name[0] == '"' && name[len - 1] == '"') ||
	                 (name[0] == '\'' && name[len - 1] == '\''))) {
		name++;
		len -= 2;
	}
	if (len == 0) {
		return NULL;
	}

	const struct editorSyntaxLanguageDef *by_name = editorSyntaxLookupLanguageByName(name, len);
	if (by_name != NULL) {
		return by_name;
	}

	for (int i = 0; i < ROTIDE_LANGUAGE_DEF_COUNT; i++) {
		const struct editorSyntaxLanguageDef *def = &g_languages[i];
		if (def->injection_aliases == NULL) {
			continue;
		}
		for (const char *const *p = def->injection_aliases; *p != NULL; p++) {
			if (languagesStringEqualsNoCaseLen(name, len, *p)) {
				return def;
			}
		}
	}
	return NULL;
}

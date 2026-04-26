#include "test_case.h"
#include "test_support.h"

static int test_editor_syntax_activation_for_c_and_h_files(void) {
	char c_path[] = "/tmp/rotide-test-syntax-c-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(c_path, 2,
			"tests/syntax/supported/c/activation.c"));

	editorOpen(c_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("translation_unit", editorSyntaxRootType());

	char h_path[] = "/tmp/rotide-test-syntax-h-XXXXXX.h";
	ASSERT_TRUE(write_fixture_to_temp_path(h_path, 2,
			"tests/syntax/supported/c/activation.h"));

	editorOpen(h_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("translation_unit", editorSyntaxRootType());

	char cpp_path[] = "/tmp/rotide-test-syntax-cpp-XXXXXX.cpp";
	ASSERT_TRUE(write_fixture_to_temp_path(cpp_path, 4,
			"tests/syntax/supported/cpp/activation.cpp"));

	editorOpen(cpp_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CPP, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("translation_unit", editorSyntaxRootType());

	char hpp_path[] = "/tmp/rotide-test-syntax-hpp-XXXXXX.hpp";
	ASSERT_TRUE(write_fixture_to_temp_path(hpp_path, 4,
			"tests/syntax/supported/cpp/activation.hpp"));

	editorOpen(hpp_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CPP, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("translation_unit", editorSyntaxRootType());

	ASSERT_TRUE(unlink(c_path) == 0);
	ASSERT_TRUE(unlink(h_path) == 0);
	ASSERT_TRUE(unlink(cpp_path) == 0);
	ASSERT_TRUE(unlink(hpp_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_shell_files_and_shebang(void) {
	char sh_path[] = "/tmp/rotide-test-syntax-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(sh_path, 3,
			"tests/syntax/supported/bash/activation.sh"));

	editorOpen(sh_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);

	char rc_dir_template[] = "/tmp/rotide-test-syntax-shell-rc-XXXXXX";
	char *rc_dir = mkdtemp(rc_dir_template);
	ASSERT_TRUE(rc_dir != NULL);
	char rc_path[512];
	ASSERT_TRUE(path_join(rc_path, sizeof(rc_path), rc_dir, ".bashrc"));
	ASSERT_TRUE(copyTestFixtureToPath("tests/syntax/supported/bash/.bashrc", rc_path));

	ASSERT_TRUE(editorTabsInit());
	editorOpen(rc_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	char shebang_path[] = "/tmp/rotide-test-syntax-shell-shebang-XXXXXX";
	ASSERT_TRUE(write_fixture_to_temp_path(shebang_path, 0,
			"tests/syntax/supported/bash/extensionless_shebang"));

	ASSERT_TRUE(editorTabsInit());
	editorOpen(shebang_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	char plain_path[] = "/tmp/rotide-test-syntax-shell-plain-XXXXXX";
	int plain_fd = mkstemp(plain_path);
	ASSERT_TRUE(plain_fd != -1);
	const char *plain_source = "echo plain\n";
	ASSERT_TRUE(write_all(plain_fd, plain_source, strlen(plain_source)) == 0);
	ASSERT_TRUE(close(plain_fd) == 0);

	ASSERT_TRUE(editorTabsInit());
	editorOpen(plain_path);
	ASSERT_TRUE(!editorSyntaxEnabled());
	ASSERT_TRUE(!editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(sh_path) == 0);
	ASSERT_TRUE(unlink(rc_path) == 0);
	ASSERT_TRUE(unlink(shebang_path) == 0);
	ASSERT_TRUE(unlink(plain_path) == 0);
	ASSERT_TRUE(rmdir(rc_dir) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_html_js_and_css_files(void) {
	char html_path[] = "/tmp/rotide-test-syntax-html-XXXXXX.html";
	ASSERT_TRUE(write_fixture_to_temp_path(html_path, 5,
			"tests/syntax/supported/html/activation.html"));

	editorOpen(html_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());
	ASSERT_EQ_STR("document", editorSyntaxRootType());

	char htm_path[] = "/tmp/rotide-test-syntax-html2-XXXXXX.htm";
	ASSERT_TRUE(write_fixture_to_temp_path(htm_path, 4,
			"tests/syntax/supported/html/activation.htm"));
	editorOpen(htm_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	char xhtml_path[] = "/tmp/rotide-test-syntax-xhtml-XXXXXX.xhtml";
	ASSERT_TRUE(write_fixture_to_temp_path(xhtml_path, 6,
			"tests/syntax/supported/html/activation.xhtml"));
	editorOpen(xhtml_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	char js_path[] = "/tmp/rotide-test-syntax-js-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(js_path, 3,
			"tests/syntax/supported/javascript/activation.js"));
	editorOpen(js_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	char mjs_path[] = "/tmp/rotide-test-syntax-mjs-XXXXXX.mjs";
	ASSERT_TRUE(write_fixture_to_temp_path(mjs_path, 4,
			"tests/syntax/supported/javascript/module.mjs"));
	editorOpen(mjs_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	char cjs_path[] = "/tmp/rotide-test-syntax-cjs-XXXXXX.cjs";
	ASSERT_TRUE(write_fixture_to_temp_path(cjs_path, 4,
			"tests/syntax/supported/javascript/commonjs.cjs"));
	editorOpen(cjs_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	char jsx_path[] = "/tmp/rotide-test-syntax-jsx-XXXXXX.jsx";
	ASSERT_TRUE(write_fixture_to_temp_path(jsx_path, 4,
			"tests/syntax/supported/javascript/component.jsx"));
	editorOpen(jsx_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	char css_path[] = "/tmp/rotide-test-syntax-css-XXXXXX.css";
	ASSERT_TRUE(write_fixture_to_temp_path(css_path, 4,
			"tests/syntax/supported/css/activation.css"));
	editorOpen(css_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());

	char scss_path[] = "/tmp/rotide-test-syntax-scss-XXXXXX.scss";
	ASSERT_TRUE(write_fixture_to_temp_path(scss_path, 5,
			"tests/syntax/supported/css/activation.scss"));
	editorOpen(scss_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(html_path) == 0);
	ASSERT_TRUE(unlink(htm_path) == 0);
	ASSERT_TRUE(unlink(xhtml_path) == 0);
	ASSERT_TRUE(unlink(js_path) == 0);
	ASSERT_TRUE(unlink(mjs_path) == 0);
	ASSERT_TRUE(unlink(cjs_path) == 0);
	ASSERT_TRUE(unlink(jsx_path) == 0);
	ASSERT_TRUE(unlink(css_path) == 0);
	ASSERT_TRUE(unlink(scss_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_json_files(void) {
	char json_path[] = "/tmp/rotide-test-syntax-json-XXXXXX.json";
	ASSERT_TRUE(write_fixture_to_temp_path(json_path, 5,
			"tests/syntax/supported/json/activation.json"));

	editorOpen(json_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JSON, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("document", editorSyntaxRootType());

	char jsonc_path[] = "/tmp/rotide-test-syntax-jsonc-XXXXXX.jsonc";
	ASSERT_TRUE(write_fixture_to_temp_path(jsonc_path, 6,
			"tests/syntax/supported/json/activation.json"));
	editorOpen(jsonc_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JSON, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(json_path) == 0);
	ASSERT_TRUE(unlink(jsonc_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_python_files_and_shebang(void) {
	char py_path[] = "/tmp/rotide-test-syntax-py-XXXXXX.py";
	ASSERT_TRUE(write_fixture_to_temp_path(py_path, 3,
			"tests/syntax/supported/python/activation.py"));

	editorOpen(py_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_PYTHON, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("module", editorSyntaxRootType());

	char pyi_path[] = "/tmp/rotide-test-syntax-pyi-XXXXXX.pyi";
	ASSERT_TRUE(write_fixture_to_temp_path(pyi_path, 4,
			"tests/syntax/supported/python/activation.pyi"));
	editorOpen(pyi_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_PYTHON, editorSyntaxLanguageActive());

	char pyw_path[] = "/tmp/rotide-test-syntax-pyw-XXXXXX.pyw";
	ASSERT_TRUE(write_fixture_to_temp_path(pyw_path, 4,
			"tests/syntax/supported/python/activation.pyw"));
	editorOpen(pyw_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_PYTHON, editorSyntaxLanguageActive());

	char shebang_path[] = "/tmp/rotide-test-syntax-py-shebang-XXXXXX";
	ASSERT_TRUE(write_fixture_to_temp_path(shebang_path, 0,
			"tests/syntax/supported/python/extensionless_shebang"));

	ASSERT_TRUE(editorTabsInit());
	editorOpen(shebang_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_PYTHON, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(py_path) == 0);
	ASSERT_TRUE(unlink(pyi_path) == 0);
	ASSERT_TRUE(unlink(pyw_path) == 0);
	ASSERT_TRUE(unlink(shebang_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_typescript_files(void) {
	char ts_path[] = "/tmp/rotide-test-syntax-ts-XXXXXX.ts";
	ASSERT_TRUE(write_fixture_to_temp_path(ts_path, 3,
			"tests/syntax/supported/typescript/activation.ts"));

	editorOpen(ts_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_TYPESCRIPT, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("program", editorSyntaxRootType());

	char tsx_path[] = "/tmp/rotide-test-syntax-tsx-XXXXXX.tsx";
	ASSERT_TRUE(write_fixture_to_temp_path(tsx_path, 4,
			"tests/syntax/supported/typescript/activation.tsx"));

	editorOpen(tsx_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_TYPESCRIPT, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(ts_path) == 0);
	ASSERT_TRUE(unlink(tsx_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_php_files_and_shebang(void) {
	char php_path[] = "/tmp/rotide-test-syntax-php-XXXXXX.php";
	ASSERT_TRUE(write_fixture_to_temp_path(php_path, 4,
			"tests/syntax/supported/php/activation.php"));

	editorOpen(php_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_PHP, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("program", editorSyntaxRootType());

	char phtml_path[] = "/tmp/rotide-test-syntax-phtml-XXXXXX.phtml";
	ASSERT_TRUE(write_fixture_to_temp_path(phtml_path, 6,
			"tests/syntax/supported/php/activation.phtml"));
	editorOpen(phtml_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_PHP, editorSyntaxLanguageActive());

	char shebang_path[] = "/tmp/rotide-test-syntax-php-shebang-XXXXXX";
	ASSERT_TRUE(write_fixture_to_temp_path(shebang_path, 0,
			"tests/syntax/supported/php/extensionless_shebang"));

	ASSERT_TRUE(editorTabsInit());
	editorOpen(shebang_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_PHP, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(php_path) == 0);
	ASSERT_TRUE(unlink(phtml_path) == 0);
	ASSERT_TRUE(unlink(shebang_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_rust_files(void) {
	char rs_path[] = "/tmp/rotide-test-syntax-rs-XXXXXX.rs";
	ASSERT_TRUE(write_fixture_to_temp_path(rs_path, 3,
			"tests/syntax/supported/rust/activation.rs"));

	editorOpen(rs_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_RUST, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("source_file", editorSyntaxRootType());

	ASSERT_TRUE(unlink(rs_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_java_files(void) {
	char java_path[] = "/tmp/rotide-test-syntax-java-XXXXXX.java";
	ASSERT_TRUE(write_fixture_to_temp_path(java_path, 5,
			"tests/syntax/supported/java/activation.java"));

	editorOpen(java_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVA, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("program", editorSyntaxRootType());

	ASSERT_TRUE(unlink(java_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_csharp_files(void) {
	char cs_path[] = "/tmp/rotide-test-syntax-csharp-XXXXXX.cs";
	ASSERT_TRUE(write_fixture_to_temp_path(cs_path, 3,
			"tests/syntax/supported/csharp/activation.cs"));

	editorOpen(cs_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSHARP, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("compilation_unit", editorSyntaxRootType());

	ASSERT_TRUE(unlink(cs_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_haskell_files(void) {
	char hs_path[] = "/tmp/rotide-test-syntax-haskell-XXXXXX.hs";
	ASSERT_TRUE(write_fixture_to_temp_path(hs_path, 3,
			"tests/syntax/supported/haskell/activation.hs"));

	editorOpen(hs_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HASKELL, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("haskell", editorSyntaxRootType());

	ASSERT_TRUE(unlink(hs_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_ruby_files(void) {
	char rb_path[] = "/tmp/rotide-test-syntax-ruby-XXXXXX.rb";
	ASSERT_TRUE(write_fixture_to_temp_path(rb_path, 3,
			"tests/syntax/supported/ruby/activation.rb"));

	editorOpen(rb_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_RUBY, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("program", editorSyntaxRootType());

	ASSERT_TRUE(unlink(rb_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_ocaml_files(void) {
	char ml_path[] = "/tmp/rotide-test-syntax-ocaml-XXXXXX.ml";
	ASSERT_TRUE(write_fixture_to_temp_path(ml_path, 3,
			"tests/syntax/supported/ocaml/activation.ml"));

	editorOpen(ml_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_OCAML, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("compilation_unit", editorSyntaxRootType());

	ASSERT_TRUE(unlink(ml_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_ejs_files(void) {
	char ejs_path[] = "/tmp/rotide-test-syntax-ejs-XXXXXX.ejs";
	ASSERT_TRUE(write_fixture_to_temp_path(ejs_path, 4,
			"tests/syntax/supported/ejs/activation.ejs"));

	editorOpen(ejs_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_EJS, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("template", editorSyntaxRootType());

	ASSERT_TRUE(unlink(ejs_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_erb_files(void) {
	char erb_path[] = "/tmp/rotide-test-syntax-erb-XXXXXX.erb";
	ASSERT_TRUE(write_fixture_to_temp_path(erb_path, 4,
			"tests/syntax/supported/erb/activation.erb"));

	editorOpen(erb_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_ERB, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("template", editorSyntaxRootType());

	ASSERT_TRUE(unlink(erb_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_scala_files(void) {
	char scala_path[] = "/tmp/rotide-test-syntax-scala-XXXXXX.scala";
	ASSERT_TRUE(write_fixture_to_temp_path(scala_path, 6,
			"tests/syntax/supported/scala/activation.scala"));

	editorOpen(scala_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SCALA, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("compilation_unit", editorSyntaxRootType());

	ASSERT_TRUE(unlink(scala_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_julia_files(void) {
	char jl_path[] = "/tmp/rotide-test-syntax-julia-XXXXXX.jl";
	ASSERT_TRUE(write_fixture_to_temp_path(jl_path, 3,
			"tests/syntax/supported/julia/activation.jl"));

	editorOpen(jl_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JULIA, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("source_file", editorSyntaxRootType());

	ASSERT_TRUE(unlink(jl_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_regex_files(void) {
	char regex_path[] = "/tmp/rotide-test-syntax-regex-XXXXXX.regex";
	ASSERT_TRUE(write_fixture_to_temp_path(regex_path, 6,
			"tests/syntax/supported/regex/activation.regex"));

	editorOpen(regex_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_REGEX, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("pattern", editorSyntaxRootType());

	ASSERT_TRUE(unlink(regex_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_go_and_mod_files(void) {
	char go_path[] = "/tmp/rotide-test-syntax-go-XXXXXX.go";
	ASSERT_TRUE(write_fixture_to_temp_path(go_path, 3,
			"tests/syntax/supported/go/activation.go"));

	editorOpen(go_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("source_file", editorSyntaxRootType());

	char mod_dir_template[] = "/tmp/rotide-test-syntax-go-mod-XXXXXX";
	char *mod_dir = mkdtemp(mod_dir_template);
	ASSERT_TRUE(mod_dir != NULL);

	char mod_path[512];
	ASSERT_TRUE(path_join(mod_path, sizeof(mod_path), mod_dir, "go.mod"));
	ASSERT_TRUE(copyTestFixtureToPath("tests/syntax/supported/go/go.mod", mod_path));

	editorOpen(mod_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	char sum_path[512];
	ASSERT_TRUE(path_join(sum_path, sizeof(sum_path), mod_dir, "go.sum"));
	ASSERT_TRUE(copyTestFixtureToPath("tests/syntax/supported/go/go.sum", sum_path));

	editorOpen(sum_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(go_path) == 0);
	ASSERT_TRUE(unlink(mod_path) == 0);
	ASSERT_TRUE(unlink(sum_path) == 0);
	ASSERT_TRUE(rmdir(mod_dir) == 0);
	return 0;
}

static int test_editor_syntax_disabled_for_non_c_or_shell_files(void) {
	char path[] = "/tmp/rotide-test-syntax-txt-XXXXXX.txt";
	int fd = mkstemps(path, 4);
	ASSERT_TRUE(fd != -1);
	const char *text_source = "#!/usr/bin/env bash\necho not-shell-because-extension\n";
	ASSERT_TRUE(write_all(fd, text_source, strlen(text_source)) == 0);
	ASSERT_TRUE(close(fd) == 0);

	editorOpen(path);
	ASSERT_TRUE(!editorSyntaxEnabled());
	ASSERT_TRUE(!editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() == NULL);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_save_as_shell_and_non_shell_updates_syntax(void) {
	char shell_path[] = "/tmp/rotide-test-syntax-saveas-shell-XXXXXX.sh";
	int shell_fd = mkstemps(shell_path, 3);
	ASSERT_TRUE(shell_fd != -1);
	ASSERT_TRUE(close(shell_fd) == 0);
	ASSERT_TRUE(unlink(shell_path) == 0);

	char txt_path[] = "/tmp/rotide-test-syntax-saveas-shell-XXXXXX.txt";
	int txt_fd = mkstemps(txt_path, 4);
	ASSERT_TRUE(txt_fd != -1);
	ASSERT_TRUE(close(txt_fd) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);

	add_row("#!/usr/bin/env bash");
	add_row("echo \"$HOME\"");
	E.dirty = 1;
	ASSERT_TRUE(E.filename == NULL);

	char shell_input[256];
	int shell_written = snprintf(shell_input, sizeof(shell_input), "%s\r", shell_path);
	ASSERT_TRUE(shell_written > 0 && (size_t)shell_written < sizeof(shell_input));

	int saved_stdin;
	int saved_stdout;
	ASSERT_TRUE(setup_stdin_bytes(shell_input, (size_t)shell_written, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	E.dirty = 1;
	free(E.filename);
	E.filename = NULL;
	char txt_input[256];
	int txt_written = snprintf(txt_input, sizeof(txt_input), "%s\r", txt_path);
	ASSERT_TRUE(txt_written > 0 && (size_t)txt_written < sizeof(txt_input));

	ASSERT_TRUE(setup_stdin_bytes(txt_input, (size_t)txt_written, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_TRUE(!editorSyntaxEnabled());
	ASSERT_TRUE(!editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(shell_path) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);
	return 0;
}

static int test_editor_save_as_web_and_plain_updates_syntax(void) {
	char html_path[] = "/tmp/rotide-test-syntax-saveas-web-XXXXXX.html";
	int html_fd = mkstemps(html_path, 5);
	ASSERT_TRUE(html_fd != -1);
	ASSERT_TRUE(close(html_fd) == 0);
	ASSERT_TRUE(unlink(html_path) == 0);

	char js_path[] = "/tmp/rotide-test-syntax-saveas-web-XXXXXX.js";
	int js_fd = mkstemps(js_path, 3);
	ASSERT_TRUE(js_fd != -1);
	ASSERT_TRUE(close(js_fd) == 0);
	ASSERT_TRUE(unlink(js_path) == 0);

	char css_path[] = "/tmp/rotide-test-syntax-saveas-web-XXXXXX.css";
	int css_fd = mkstemps(css_path, 4);
	ASSERT_TRUE(css_fd != -1);
	ASSERT_TRUE(close(css_fd) == 0);
	ASSERT_TRUE(unlink(css_path) == 0);

	char txt_path[] = "/tmp/rotide-test-syntax-saveas-web-XXXXXX.txt";
	int txt_fd = mkstemps(txt_path, 4);
	ASSERT_TRUE(txt_fd != -1);
	ASSERT_TRUE(close(txt_fd) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);

	add_row("<div class=\"x\">hi</div>");
	E.dirty = 1;
	ASSERT_TRUE(E.filename == NULL);

	int saved_stdin;
	int saved_stdout;

	char input_html[256];
	int written_html = snprintf(input_html, sizeof(input_html), "%s\r", html_path);
	ASSERT_TRUE(written_html > 0 && (size_t)written_html < sizeof(input_html));
	ASSERT_TRUE(setup_stdin_bytes(input_html, (size_t)written_html, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.dirty = 1;
	free(E.filename);
	E.filename = NULL;
	char input_js[256];
	int written_js = snprintf(input_js, sizeof(input_js), "%s\r", js_path);
	ASSERT_TRUE(written_js > 0 && (size_t)written_js < sizeof(input_js));
	ASSERT_TRUE(setup_stdin_bytes(input_js, (size_t)written_js, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.dirty = 1;
	free(E.filename);
	E.filename = NULL;
	char input_css[256];
	int written_css = snprintf(input_css, sizeof(input_css), "%s\r", css_path);
	ASSERT_TRUE(written_css > 0 && (size_t)written_css < sizeof(input_css));
	ASSERT_TRUE(setup_stdin_bytes(input_css, (size_t)written_css, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.dirty = 1;
	free(E.filename);
	E.filename = NULL;
	char input_txt[256];
	int written_txt = snprintf(input_txt, sizeof(input_txt), "%s\r", txt_path);
	ASSERT_TRUE(written_txt > 0 && (size_t)written_txt < sizeof(input_txt));
	ASSERT_TRUE(setup_stdin_bytes(input_txt, (size_t)written_txt, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());
	ASSERT_TRUE(!editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(html_path) == 0);
	ASSERT_TRUE(unlink(js_path) == 0);
	ASSERT_TRUE(unlink(css_path) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);
	return 0;
}

static int test_editor_save_as_c_file_enables_syntax(void) {
	char path[] = "/tmp/rotide-test-syntax-saveas-XXXXXX.c";
	int fd = mkstemps(path, 2);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);
	ASSERT_TRUE(unlink(path) == 0);

	add_row("int main(void) { return 0; }");
	E.dirty = 1;
	ASSERT_TRUE(E.filename == NULL);

	char input[256];
	int written = snprintf(input, sizeof(input), "%s\r", path);
	ASSERT_TRUE(written > 0);
	ASSERT_TRUE((size_t)written < sizeof(input));

	int saved_stdin;
	int saved_stdout;
	ASSERT_TRUE(setup_stdin_bytes(input, (size_t)written, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);

	editorSave();

	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_save_as_go_file_enables_syntax(void) {
	char path[] = "/tmp/rotide-test-syntax-saveas-go-XXXXXX.go";
	int fd = mkstemps(path, 3);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);
	ASSERT_TRUE(unlink(path) == 0);

	add_row("package main");
	add_row("");
	add_row("func main() { var n int = 1 }");
	E.dirty = 1;
	ASSERT_TRUE(E.filename == NULL);

	char input[256];
	int written = snprintf(input, sizeof(input), "%s\r", path);
	ASSERT_TRUE(written > 0);
	ASSERT_TRUE((size_t)written < sizeof(input));

	int saved_stdin;
	int saved_stdout;
	ASSERT_TRUE(setup_stdin_bytes(input, (size_t)written, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);

	editorSave();

	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("source_file", editorSyntaxRootType());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2,
			"tests/syntax/supported/c/incremental.c"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());

	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorSelectionRange delete_line = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_line));
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_cpp_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-cpp-XXXXXX.cpp";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/cpp/incremental.cpp"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CPP, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());

	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorSelectionRange delete_line = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_line));
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_shell_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/bash/incremental.sh"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());

	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorSelectionRange delete_line = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_line));
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_html_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-html-XXXXXX.html";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
			"tests/syntax/supported/html/incremental.html"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 6;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 6;
	E.cx = 6;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorSelectionRange delete_line = {
		.start_cy = 1,
		.start_cx = 0,
		.end_cy = 2,
		.end_cx = 0
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_line));
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_javascript_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-js-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/javascript/incremental.js"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_typescript_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-ts-XXXXXX.ts";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/typescript/incremental.ts"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_TYPESCRIPT, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_css_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-css-XXXXXX.css";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/css/incremental.css"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_go_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-go-XXXXXX.go";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/go/incremental.go"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 2;
	E.cx = E.rows[2].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_python_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-py-XXXXXX.py";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/python/incremental.py"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_PYTHON, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 4;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_php_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-php-XXXXXX.php";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/php/incremental.php"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_PHP, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 4;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_rust_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-rs-XXXXXX.rs";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/rust/incremental.rs"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_RUST, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 4;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_java_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-java-XXXXXX.java";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
			"tests/syntax/supported/java/incremental.java"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVA, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 8;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_csharp_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-csharp-XXXXXX.cs";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/csharp/incremental.cs"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSHARP, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 8;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_haskell_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-haskell-XXXXXX.hs";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/haskell/incremental.hs"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HASKELL, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 6;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_ruby_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-ruby-XXXXXX.rb";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/ruby/incremental.rb"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_RUBY, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 3;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_ocaml_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-ocaml-XXXXXX.ml";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/ocaml/incremental.ml"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_OCAML, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 3;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_ejs_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-ejs-XXXXXX.ejs";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/ejs/incremental.ejs"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_EJS, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 4;
	editorInsertChar(' ');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_erb_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-erb-XXXXXX.erb";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/erb/incremental.erb"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_ERB, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 4;
	editorInsertChar(' ');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_scala_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-scala-XXXXXX.scala";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 6,
			"tests/syntax/supported/scala/incremental.scala"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SCALA, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 5;
	editorInsertChar('y');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_julia_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-julia-XXXXXX.jl";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/julia/incremental.jl"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JULIA, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 1;
	editorInsertChar('y');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_regex_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-regex-XXXXXX.regex";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 6,
			"tests/syntax/supported/regex/incremental.regex"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_REGEX, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 3;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_query_budget_match_limit_is_graceful(void) {
	size_t source_len = 0;
	char *source = build_repeated_text("const value = document + window;\n", 512, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len <= UINT32_MAX);
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, source_len);

	editorSyntaxTestSetBudgetOverrides(1, 1, 0, 2000000000ULL);
	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	int parse_budget = 0;
	int query_budget = 0;
	(void)editorSyntaxStateConsumeBudgetEvents(state, &parse_budget, &query_budget);

	struct editorSyntaxCapture captures[1024];
	int capture_count = 0;
	ASSERT_TRUE(editorSyntaxStateCollectCapturesForRange(state, &source_view, 0,
				(uint32_t)source_len, captures,
				(int)(sizeof(captures) / sizeof(captures[0])), &capture_count));
	ASSERT_TRUE(editorSyntaxStateConsumeBudgetEvents(state, &parse_budget, &query_budget));
	ASSERT_EQ_INT(0, parse_budget);
	ASSERT_EQ_INT(1, query_budget);

	editorSyntaxStateDestroy(state);
	editorSyntaxTestResetBudgetOverrides();
	free(source);
	return 0;
}

static int test_editor_syntax_query_compile_failure_records_diagnostics(void) {
	const char *broken_query = "((identifier) @name";
	editorSyntaxTestResetLastQueryCompileError();

	ASSERT_EQ_INT(0, editorSyntaxTestCompileQueryForDiagnostics(
				EDITOR_SYNTAX_C, broken_query));

	struct editorSyntaxQueryCompileError error = {0};
	ASSERT_TRUE(editorSyntaxCopyLastQueryCompileError(&error));
	ASSERT_EQ_INT(1, error.has_error);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, error.language);
	ASSERT_TRUE(error.error_type != 0);
	ASSERT_TRUE(error.error_offset <= strlen(broken_query));
	ASSERT_TRUE(strstr(error.context, "identifier") != NULL);

	struct editorSyntaxQueryCompileError drained = {0};
	ASSERT_TRUE(editorSyntaxDrainLastQueryCompileError(&drained));
	ASSERT_EQ_INT(error.language, drained.language);
	ASSERT_EQ_INT(error.error_type, drained.error_type);
	ASSERT_EQ_INT(error.error_offset, drained.error_offset);
	ASSERT_EQ_STR(error.context, drained.context);
	ASSERT_TRUE(!editorSyntaxDrainLastQueryCompileError(NULL));

	editorSyntaxTestResetLastQueryCompileError();
	return 0;
}

static int test_editor_syntax_collect_c_captures_without_injection_query_is_graceful(void) {
	const char *source = "int value = 1;\n";
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, strlen(source));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_C);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	struct editorSyntaxCapture captures[32];
	int capture_count = 0;
	ASSERT_TRUE(editorSyntaxStateCollectCapturesForRange(state, &source_view, 0,
				(uint32_t)strlen(source), captures,
				(int)(sizeof(captures) / sizeof(captures[0])), &capture_count));
	ASSERT_TRUE(capture_count > 0);
	ASSERT_TRUE(!editorSyntaxStateConsumeQueryUnavailableEvent(state, NULL, NULL));

	editorSyntaxStateDestroy(state);
	return 0;
}

static char *build_dense_c_expression_source(int terms, size_t *len_out) {
	if (terms <= 0 || len_out == NULL) {
		return NULL;
	}

	const char *prefix = "int value = ";
	const char *suffix = ";\n";
	size_t cap = strlen(prefix) + strlen(suffix) + (size_t)terms * 16 + 1;
	char *source = malloc(cap);
	if (source == NULL) {
		return NULL;
	}

	size_t used = 0;
	int written = snprintf(source + used, cap - used, "%s", prefix);
	if (written < 0 || (size_t)written >= cap - used) {
		free(source);
		return NULL;
	}
	used += (size_t)written;
	for (int i = 0; i < terms; i++) {
		written = snprintf(source + used, cap - used, "%s%d",
				i == 0 ? "" : " + ", i);
		if (written < 0 || (size_t)written >= cap - used) {
			free(source);
			return NULL;
		}
		used += (size_t)written;
	}
	written = snprintf(source + used, cap - used, "%s", suffix);
	if (written < 0 || (size_t)written >= cap - used) {
		free(source);
		return NULL;
	}
	used += (size_t)written;
	*len_out = used;
	return source;
}

static int test_editor_syntax_capture_truncation_reports_event_and_keeps_span_limit(void) {
	size_t source_len = 0;
	char *source = build_dense_c_expression_source(300, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len <= UINT32_MAX);

	char path[512];
	ASSERT_TRUE(write_temp_c_file(path, sizeof(path), source));
	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorRowSyntaxSpan spans[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int span_count = 0;
	ASSERT_TRUE(editorSyntaxRowRenderSpans(0, spans,
				(int)(sizeof(spans) / sizeof(spans[0])), &span_count));
	ASSERT_EQ_INT(ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, span_count);

	for (int i = 0; i < span_count; i++) {
		ASSERT_TRUE(spans[i].end_render_idx > spans[i].start_render_idx);
		ASSERT_TRUE(spans[i].highlight_class != EDITOR_SYNTAX_HL_NONE);
	}

	struct editorSyntaxLimitEvent event = {0};
	ASSERT_TRUE(editorSyntaxStateConsumeLimitEvent(E.syntax_state, &event));
	ASSERT_EQ_INT(EDITOR_SYNTAX_LIMIT_EVENT_CAPTURE_TRUNCATED, event.kind);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, event.language);
	ASSERT_EQ_INT(-1, event.row);
	ASSERT_EQ_INT(ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, event.detail);

	E.window_rows = 4;
	E.window_cols = 120;
	E.rowoff = 0;
	E.coloff = 0;
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	ASSERT_TRUE(strstr(E.statusmsg, "Tree-sitter syntax spans truncated") != NULL);

	span_count = 0;
	ASSERT_TRUE(editorSyntaxRowRenderSpans(0, spans,
				(int)(sizeof(spans) / sizeof(spans[0])), &span_count));
	ASSERT_EQ_INT(ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, span_count);

	ASSERT_TRUE(unlink(path) == 0);
	free(source);
	return 0;
}

static int test_editor_syntax_parse_budget_is_graceful(void) {
	size_t source_len = 0;
	char *source = build_repeated_text("function item(){ return 1; }\n", 120000, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len <= UINT32_MAX);
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, source_len);

	editorSyntaxTestSetBudgetOverrides(1, 8192, 2000000000ULL, 1);
	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	int parse_budget = 0;
	int query_budget = 0;
	ASSERT_TRUE(editorSyntaxStateConsumeBudgetEvents(state, &parse_budget, &query_budget));
	ASSERT_EQ_INT(1, parse_budget);
	ASSERT_EQ_INT(0, query_budget);

	editorSyntaxStateDestroy(state);
	editorSyntaxTestResetBudgetOverrides();
	free(source);
	return 0;
}

static int test_editor_syntax_incremental_provider_parse_keeps_tree_valid(void) {
	const char *before = "int value = 1;\n";
	const char *after = "int xvalue = 1;\n";
	struct editorTextSource before_source = {0};
	struct editorTextSource after_source = {0};
	editorTextSourceInitString(&before_source, before, strlen(before));
	editorTextSourceInitString(&after_source, after, strlen(after));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_C);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &before_source));

	struct editorSyntaxEdit edit = {
		.start_byte = 4,
		.old_end_byte = 4,
		.new_end_byte = 5,
		.start_point = {.row = 0, .column = 4},
		.old_end_point = {.row = 0, .column = 4},
		.new_end_point = {.row = 0, .column = 5}
	};
	ASSERT_TRUE(editorSyntaxStateApplyEditAndParse(state, &edit, &after_source));
	ASSERT_TRUE(editorSyntaxStateHasTree(state));

	struct editorSyntaxCapture captures[32];
	int capture_count = 0;
	ASSERT_TRUE(editorSyntaxStateCollectCapturesForRange(state, &after_source, 0,
				(uint32_t)strlen(after), captures,
				(int)(sizeof(captures) / sizeof(captures[0])), &capture_count));
	ASSERT_TRUE(capture_count > 0);

	editorSyntaxStateDestroy(state);
	return 0;
}

static int test_editor_syntax_large_file_stays_enabled_in_degraded_mode(void) {
	size_t source_len = 0;
	char *source = build_repeated_text("int value = 1;\n", 600000, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len > (size_t)(8 * 1024 * 1024));
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, source_len);

	editorSyntaxTestSetBudgetOverrides(1, 8192, 0, 0);
	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_C);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateConfigureForSourceLength(state, source_len));
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));
	ASSERT_TRUE(editorSyntaxStateHasTree(state));
	ASSERT_EQ_INT(EDITOR_SYNTAX_PERF_DEGRADED_INJECTIONS,
			editorSyntaxStatePerformanceMode(state));

	editorSyntaxStateDestroy(state);
	editorSyntaxTestResetBudgetOverrides();
	free(source);
	return 0;
}

static int test_editor_syntax_visible_cache_recomputes_only_changed_rows(void) {
	char path[] = "/tmp/rotide-test-syntax-visible-cache-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2,
			"tests/syntax/supported/c/visible_cache.c"));

	editorOpen(path);
	E.window_rows = 6;
	E.window_cols = 100;
	E.rowoff = 0;
	E.coloff = 0;
	E.cy = 1;
	E.cx = 2;

	editorSyntaxTestResetVisibleRowRecomputeCount();
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	int full_recompute = editorSyntaxTestVisibleRowRecomputeCount();
	ASSERT_TRUE(full_recompute > 0);

	editorSyntaxTestResetVisibleRowRecomputeCount();
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	int incremental_recompute = editorSyntaxTestVisibleRowRecomputeCount();
	ASSERT_TRUE(incremental_recompute > 0);
	ASSERT_TRUE(incremental_recompute < full_recompute);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_undo_redo_preserves_tree(void) {
	char path[] = "/tmp/rotide-test-syntax-history-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2,
			"tests/syntax/supported/c/history.c"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorHistoryBeginEdit(EDITOR_EDIT_NEWLINE);
	int dirty_before = E.dirty;
	editorInsertNewline();
	editorHistoryCommitEdit(EDITOR_EDIT_NEWLINE, E.dirty != dirty_before);
	editorHistoryBreakGroup();

	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(1, editorRedo());
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_undo_redo_preserves_shell_tree(void) {
	char path[] = "/tmp/rotide-test-syntax-history-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/bash/history.sh"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = E.rows[1].size;
	editorHistoryBeginEdit(EDITOR_EDIT_NEWLINE);
	int dirty_before = E.dirty;
	editorInsertNewline();
	editorHistoryCommitEdit(EDITOR_EDIT_NEWLINE, E.dirty != dirty_before);
	editorHistoryBreakGroup();

	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(1, editorRedo());
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_tabs_keep_independent_syntax_states(void) {
	ASSERT_TRUE(editorTabsInit());

	char c_path[] = "/tmp/rotide-test-syntax-tabs-c-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(c_path, 2,
			"tests/syntax/supported/c/activation.c"));

	char txt_path[] = "/tmp/rotide-test-syntax-tabs-txt-XXXXXX.txt";
	int txt_fd = mkstemps(txt_path, 4);
	ASSERT_TRUE(txt_fd != -1);
	const char *txt_source = "notes\n";
	ASSERT_TRUE(write_all(txt_fd, txt_source, strlen(txt_source)) == 0);
	ASSERT_TRUE(close(txt_fd) == 0);

	editorOpen(c_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabOpenFileAsNew(txt_path));
	ASSERT_TRUE(!editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_TRUE(!editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(c_path) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);
	return 0;
}

static int test_editor_tabs_keep_shell_and_c_syntax_states(void) {
	ASSERT_TRUE(editorTabsInit());

	char sh_path[] = "/tmp/rotide-test-syntax-tabs-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(sh_path, 3,
			"tests/syntax/supported/bash/tab.sh"));

	char c_path[] = "/tmp/rotide-test-syntax-tabs-c2-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(c_path, 2,
			"tests/syntax/supported/c/activation.c"));

	editorOpen(sh_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabOpenFileAsNew(c_path));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(sh_path) == 0);
	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_tabs_keep_web_and_c_syntax_states(void) {
	ASSERT_TRUE(editorTabsInit());

	char html_path[] = "/tmp/rotide-test-syntax-tabs-html-XXXXXX.html";
	ASSERT_TRUE(write_fixture_to_temp_path(html_path, 5,
			"tests/syntax/supported/html/tab.html"));

	char c_path[] = "/tmp/rotide-test-syntax-tabs-c3-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(c_path, 2,
			"tests/syntax/supported/c/activation.c"));

	editorOpen(html_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabOpenFileAsNew(c_path));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(html_path) == 0);
	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_recovery_restore_rebuilds_c_syntax_tree(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("int recovered = 1;");
	E.dirty = 1;
	E.filename = strdup("recovered.c");
	ASSERT_TRUE(E.filename != NULL);

	editorRecoveryMaybeAutosaveOnActivity();
	ASSERT_TRUE(editorRecoveryHasSnapshot());

	ASSERT_TRUE(editorTabsInit());
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(editorRecoveryRestoreSnapshot());
	ASSERT_EQ_STR("recovered.c", E.filename);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_recovery_restore_rebuilds_shell_syntax_tree(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("#!/usr/bin/env bash");
	add_row("echo restored");
	E.dirty = 1;
	E.filename = strdup("recovered.sh");
	ASSERT_TRUE(E.filename != NULL);

	editorRecoveryMaybeAutosaveOnActivity();
	ASSERT_TRUE(editorRecoveryHasSnapshot());

	ASSERT_TRUE(editorTabsInit());
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(editorRecoveryRestoreSnapshot());
	ASSERT_EQ_STR("recovered.sh", E.filename);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	cleanup_recovery_test_env(&env);
	return 0;
}

const struct editorTestCase g_syntax_tests[] = {
	{"editor_syntax_activation_for_c_and_h_files", test_editor_syntax_activation_for_c_and_h_files},
	{"editor_syntax_activation_for_shell_files_and_shebang", test_editor_syntax_activation_for_shell_files_and_shebang},
	{"editor_syntax_activation_for_html_js_and_css_files", test_editor_syntax_activation_for_html_js_and_css_files},
	{"editor_syntax_activation_for_json_files", test_editor_syntax_activation_for_json_files},
	{"editor_syntax_activation_for_typescript_files", test_editor_syntax_activation_for_typescript_files},
	{"editor_syntax_activation_for_python_files_and_shebang", test_editor_syntax_activation_for_python_files_and_shebang},
	{"editor_syntax_activation_for_php_files_and_shebang", test_editor_syntax_activation_for_php_files_and_shebang},
	{"editor_syntax_activation_for_rust_files", test_editor_syntax_activation_for_rust_files},
	{"editor_syntax_activation_for_java_files", test_editor_syntax_activation_for_java_files},
	{"editor_syntax_activation_for_csharp_files", test_editor_syntax_activation_for_csharp_files},
	{"editor_syntax_activation_for_haskell_files", test_editor_syntax_activation_for_haskell_files},
	{"editor_syntax_activation_for_ruby_files", test_editor_syntax_activation_for_ruby_files},
	{"editor_syntax_activation_for_ocaml_files", test_editor_syntax_activation_for_ocaml_files},
	{"editor_syntax_activation_for_julia_files", test_editor_syntax_activation_for_julia_files},
	{"editor_syntax_activation_for_scala_files", test_editor_syntax_activation_for_scala_files},
	{"editor_syntax_activation_for_ejs_files", test_editor_syntax_activation_for_ejs_files},
	{"editor_syntax_activation_for_erb_files", test_editor_syntax_activation_for_erb_files},
	{"editor_syntax_activation_for_regex_files", test_editor_syntax_activation_for_regex_files},
	{"editor_syntax_activation_for_go_and_mod_files", test_editor_syntax_activation_for_go_and_mod_files},
	{"editor_syntax_disabled_for_non_c_or_shell_files", test_editor_syntax_disabled_for_non_c_or_shell_files},
	{"editor_save_as_c_file_enables_syntax", test_editor_save_as_c_file_enables_syntax},
	{"editor_save_as_go_file_enables_syntax", test_editor_save_as_go_file_enables_syntax},
	{"editor_save_as_shell_and_non_shell_updates_syntax", test_editor_save_as_shell_and_non_shell_updates_syntax},
	{"editor_save_as_web_and_plain_updates_syntax", test_editor_save_as_web_and_plain_updates_syntax},
	{"editor_syntax_incremental_edits_keep_tree_valid", test_editor_syntax_incremental_edits_keep_tree_valid},
	{"editor_syntax_incremental_edits_keep_cpp_tree_valid", test_editor_syntax_incremental_edits_keep_cpp_tree_valid},
	{"editor_syntax_incremental_edits_keep_shell_tree_valid", test_editor_syntax_incremental_edits_keep_shell_tree_valid},
	{"editor_syntax_incremental_edits_keep_html_tree_valid", test_editor_syntax_incremental_edits_keep_html_tree_valid},
	{"editor_syntax_incremental_edits_keep_javascript_tree_valid", test_editor_syntax_incremental_edits_keep_javascript_tree_valid},
	{"editor_syntax_incremental_edits_keep_typescript_tree_valid", test_editor_syntax_incremental_edits_keep_typescript_tree_valid},
	{"editor_syntax_incremental_edits_keep_css_tree_valid", test_editor_syntax_incremental_edits_keep_css_tree_valid},
	{"editor_syntax_incremental_edits_keep_go_tree_valid", test_editor_syntax_incremental_edits_keep_go_tree_valid},
	{"editor_syntax_incremental_edits_keep_python_tree_valid", test_editor_syntax_incremental_edits_keep_python_tree_valid},
	{"editor_syntax_incremental_edits_keep_php_tree_valid", test_editor_syntax_incremental_edits_keep_php_tree_valid},
	{"editor_syntax_incremental_edits_keep_rust_tree_valid", test_editor_syntax_incremental_edits_keep_rust_tree_valid},
	{"editor_syntax_incremental_edits_keep_java_tree_valid", test_editor_syntax_incremental_edits_keep_java_tree_valid},
	{"editor_syntax_incremental_edits_keep_csharp_tree_valid", test_editor_syntax_incremental_edits_keep_csharp_tree_valid},
	{"editor_syntax_incremental_edits_keep_haskell_tree_valid", test_editor_syntax_incremental_edits_keep_haskell_tree_valid},
	{"editor_syntax_incremental_edits_keep_ruby_tree_valid", test_editor_syntax_incremental_edits_keep_ruby_tree_valid},
	{"editor_syntax_incremental_edits_keep_ocaml_tree_valid", test_editor_syntax_incremental_edits_keep_ocaml_tree_valid},
	{"editor_syntax_incremental_edits_keep_julia_tree_valid", test_editor_syntax_incremental_edits_keep_julia_tree_valid},
	{"editor_syntax_incremental_edits_keep_scala_tree_valid", test_editor_syntax_incremental_edits_keep_scala_tree_valid},
	{"editor_syntax_incremental_edits_keep_ejs_tree_valid", test_editor_syntax_incremental_edits_keep_ejs_tree_valid},
	{"editor_syntax_incremental_edits_keep_erb_tree_valid", test_editor_syntax_incremental_edits_keep_erb_tree_valid},
	{"editor_syntax_incremental_edits_keep_regex_tree_valid", test_editor_syntax_incremental_edits_keep_regex_tree_valid},
	{"editor_syntax_query_budget_match_limit_is_graceful", test_editor_syntax_query_budget_match_limit_is_graceful},
	{"editor_syntax_query_compile_failure_records_diagnostics", test_editor_syntax_query_compile_failure_records_diagnostics},
	{"editor_syntax_collect_c_captures_without_injection_query_is_graceful", test_editor_syntax_collect_c_captures_without_injection_query_is_graceful},
	{"editor_syntax_capture_truncation_reports_event_and_keeps_span_limit", test_editor_syntax_capture_truncation_reports_event_and_keeps_span_limit},
	{"editor_syntax_parse_budget_is_graceful", test_editor_syntax_parse_budget_is_graceful},
	{"editor_syntax_incremental_provider_parse_keeps_tree_valid", test_editor_syntax_incremental_provider_parse_keeps_tree_valid},
	{"editor_syntax_large_file_stays_enabled_in_degraded_mode", test_editor_syntax_large_file_stays_enabled_in_degraded_mode},
	{"editor_syntax_visible_cache_recomputes_only_changed_rows", test_editor_syntax_visible_cache_recomputes_only_changed_rows},
	{"editor_syntax_undo_redo_preserves_tree", test_editor_syntax_undo_redo_preserves_tree},
	{"editor_syntax_undo_redo_preserves_shell_tree", test_editor_syntax_undo_redo_preserves_shell_tree},
	{"editor_tabs_keep_independent_syntax_states", test_editor_tabs_keep_independent_syntax_states},
	{"editor_tabs_keep_shell_and_c_syntax_states", test_editor_tabs_keep_shell_and_c_syntax_states},
	{"editor_tabs_keep_web_and_c_syntax_states", test_editor_tabs_keep_web_and_c_syntax_states},
	{"editor_recovery_restore_rebuilds_c_syntax_tree", test_editor_recovery_restore_rebuilds_c_syntax_tree},
	{"editor_recovery_restore_rebuilds_shell_syntax_tree", test_editor_recovery_restore_rebuilds_shell_syntax_tree},
};

const int g_syntax_test_count =
		(int)(sizeof(g_syntax_tests) / sizeof(g_syntax_tests[0]));

#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/git_view.h"

static int test_editor_syntax_activation_for_c_and_h_files(void) {
	char c_path[] = "/tmp/rotide-test-syntax-c-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(c_path, 2, "tests/syntax/supported/c/activation.c"));

	editorOpen(c_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("translation_unit", editorSyntaxRootType());

	char h_path[] = "/tmp/rotide-test-syntax-h-XXXXXX.h";
	ASSERT_TRUE(write_fixture_to_temp_path(h_path, 2, "tests/syntax/supported/c/activation.h"));

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
	ASSERT_TRUE(write_fixture_to_temp_path(
	        shebang_path, 0, "tests/syntax/supported/bash/extensionless_shebang"));

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

	ASSERT_TRUE(unlink(ts_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_tsx_files(void) {
	char tsx_path[] = "/tmp/rotide-test-syntax-tsx-XXXXXX.tsx";
	ASSERT_TRUE(write_fixture_to_temp_path(tsx_path, 4,
	                                       "tests/syntax/supported/tsx/activation.tsx"));

	editorOpen(tsx_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_TSX, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("program", editorSyntaxRootType());

	ASSERT_TRUE(unlink(tsx_path) == 0);
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
	ASSERT_TRUE(write_fixture_to_temp_path(
	        shebang_path, 0, "tests/syntax/supported/python/extensionless_shebang"));

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

static int test_editor_syntax_activation_for_markdown_files(void) {
	char path[] = "/tmp/rotide-test-syntax-markdown-XXXXXX.md";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
	                                       "tests/syntax/supported/markdown/activation.md"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_MARKDOWN, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("document", editorSyntaxRootType());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_toml_files(void) {
	char path[] = "/tmp/rotide-test-syntax-toml-XXXXXX.toml";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 5, "tests/syntax/supported/toml/activation.toml"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_TOML, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("document", editorSyntaxRootType());

	char example_path[] = "/tmp/rotide-test-syntax-toml-example-XXXXXX.toml.example";
	ASSERT_TRUE(write_fixture_to_temp_path(example_path, 13,
	                                       "tests/syntax/supported/toml/activation.toml"));

	editorOpen(example_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_TOML, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("document", editorSyntaxRootType());

	ASSERT_TRUE(unlink(path) == 0);
	ASSERT_TRUE(unlink(example_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_yaml_files(void) {
	char yaml_path[] = "/tmp/rotide-test-syntax-yaml-XXXXXX.yaml";
	ASSERT_TRUE(write_fixture_to_temp_path(yaml_path, 5,
	                                       "tests/syntax/supported/yaml/activation.yaml"));

	editorOpen(yaml_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_YAML, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("stream", editorSyntaxRootType());

	char yml_path[] = "/tmp/rotide-test-syntax-yml-XXXXXX.yml";
	ASSERT_TRUE(write_fixture_to_temp_path(yml_path, 4,
	                                       "tests/syntax/supported/yaml/activation.yaml"));

	editorOpen(yml_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_YAML, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("stream", editorSyntaxRootType());

	char yaml_example_path[] = "/tmp/rotide-test-syntax-yaml-example-XXXXXX.yaml.example";
	ASSERT_TRUE(write_fixture_to_temp_path(yaml_example_path, 13,
	                                       "tests/syntax/supported/yaml/activation.yaml"));

	editorOpen(yaml_example_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_YAML, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("stream", editorSyntaxRootType());

	char yml_example_path[] = "/tmp/rotide-test-syntax-yml-example-XXXXXX.yml.example";
	ASSERT_TRUE(write_fixture_to_temp_path(yml_example_path, 12,
	                                       "tests/syntax/supported/yaml/activation.yaml"));

	editorOpen(yml_example_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_YAML, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("stream", editorSyntaxRootType());

	ASSERT_TRUE(unlink(yaml_path) == 0);
	ASSERT_TRUE(unlink(yml_path) == 0);
	ASSERT_TRUE(unlink(yaml_example_path) == 0);
	ASSERT_TRUE(unlink(yml_example_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_xml_files(void) {
	char xml_path[] = "/tmp/rotide-test-syntax-xml-XXXXXX.xml";
	ASSERT_TRUE(write_fixture_to_temp_path(xml_path, 4,
	                                       "tests/syntax/supported/xml/activation.xml"));

	editorOpen(xml_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_XML, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("document", editorSyntaxRootType());

	char svg_path[] = "/tmp/rotide-test-syntax-svg-XXXXXX.svg";
	ASSERT_TRUE(write_fixture_to_temp_path(svg_path, 4,
	                                       "tests/syntax/supported/xml/activation.xml"));

	editorOpen(svg_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_XML, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("document", editorSyntaxRootType());

	ASSERT_TRUE(unlink(xml_path) == 0);
	ASSERT_TRUE(unlink(svg_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_make_files(void) {
	char mk_path[] = "/tmp/rotide-test-syntax-make-XXXXXX.mk";
	ASSERT_TRUE(write_fixture_to_temp_path(mk_path, 3,
	                                       "tests/syntax/supported/make/activation.mk"));

	editorOpen(mk_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_MAKE, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("makefile", editorSyntaxRootType());

	char make_dir_template[] = "/tmp/rotide-test-syntax-makefile-XXXXXX";
	char *make_dir = mkdtemp(make_dir_template);
	ASSERT_TRUE(make_dir != NULL);

	char makefile_path[512];
	ASSERT_TRUE(path_join(makefile_path, sizeof(makefile_path), make_dir, "Makefile"));
	ASSERT_TRUE(
	        copyTestFixtureToPath("tests/syntax/supported/make/activation.mk", makefile_path));

	editorOpen(makefile_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_MAKE, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("makefile", editorSyntaxRootType());

	ASSERT_TRUE(unlink(mk_path) == 0);
	ASSERT_TRUE(unlink(makefile_path) == 0);
	ASSERT_TRUE(rmdir(make_dir) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_diff_files(void) {
	char diff_path[] = "/tmp/rotide-test-syntax-diff-XXXXXX.diff";
	ASSERT_TRUE(write_fixture_to_temp_path(diff_path, 5,
	                                       "tests/syntax/supported/diff/activation.diff"));

	editorOpen(diff_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_DIFF, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("source", editorSyntaxRootType());

	char patch_path[] = "/tmp/rotide-test-syntax-patch-XXXXXX.patch";
	ASSERT_TRUE(write_fixture_to_temp_path(patch_path, 6,
	                                       "tests/syntax/supported/diff/activation.diff"));

	editorOpen(patch_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_DIFF, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("source", editorSyntaxRootType());

	ASSERT_TRUE(unlink(diff_path) == 0);
	ASSERT_TRUE(unlink(patch_path) == 0);
	return 0;
}

static int test_editor_syntax_git_diff_tab_uses_source_language(void) {
	const char *patch = "diff --git a/src/app.c b/src/app.c\n"
	                    "index 1111111..2222222 100644\n"
	                    "--- a/src/app.c\n"
	                    "+++ b/src/app.c\n"
	                    "@@ -1,2 +1,2 @@\n"
	                    "-int old_value = 1;\n"
	                    "+int new_value = 2;\n";

	unsigned char *kinds = NULL;
	int kind_count = 0;
	char *source_path = NULL;
	char *text =
	        editorGitViewBuildDiffDup(patch, strlen(patch), &kinds, &kind_count, &source_path);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(source_path != NULL);
	ASSERT_EQ_STR("src/app.c", source_path);

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabOpenGenerated(EDITOR_TAB_GIT_DIFF, "git diff: src/app.c", text));
	free(text);
	free(E.git_view_line_kinds);
	E.git_view_line_kinds = kinds;
	E.git_view_line_kind_count = kind_count;
	free(E.git_view_source_path);
	E.git_view_source_path = source_path;
	ASSERT_TRUE(editorSyntaxParseFullActive());

	ASSERT_EQ_INT(EDITOR_TAB_GIT_DIFF, E.tab_kind);
	ASSERT_TRUE(editorActiveTabIsReadOnly());
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());
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

static int test_editor_syntax_activation_for_latex_files(void) {
	char tex_path[] = "/tmp/rotide-test-syntax-latex-XXXXXX.tex";
	ASSERT_TRUE(write_fixture_to_temp_path(tex_path, 4,
	                                       "tests/syntax/supported/latex/activation.tex"));

	editorOpen(tex_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_LATEX, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("source_file", editorSyntaxRootType());

	ASSERT_TRUE(unlink(tex_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_go_and_mod_files(void) {
	char go_path[] = "/tmp/rotide-test-syntax-go-XXXXXX.go";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(go_path, 3, "tests/syntax/supported/go/activation.go"));

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

const struct editorTestCase g_syntax_activation_tests[] = {
        {"editor_syntax_activation_for_c_and_h_files",
         test_editor_syntax_activation_for_c_and_h_files},
        {"editor_syntax_activation_for_shell_files_and_shebang",
         test_editor_syntax_activation_for_shell_files_and_shebang},
        {"editor_syntax_activation_for_html_js_and_css_files",
         test_editor_syntax_activation_for_html_js_and_css_files},
        {"editor_syntax_activation_for_json_files", test_editor_syntax_activation_for_json_files},
        {"editor_syntax_activation_for_typescript_files",
         test_editor_syntax_activation_for_typescript_files},
        {"editor_syntax_activation_for_tsx_files", test_editor_syntax_activation_for_tsx_files},
        {"editor_syntax_activation_for_python_files_and_shebang",
         test_editor_syntax_activation_for_python_files_and_shebang},
        {"editor_syntax_activation_for_php_files_and_shebang",
         test_editor_syntax_activation_for_php_files_and_shebang},
        {"editor_syntax_activation_for_rust_files", test_editor_syntax_activation_for_rust_files},
        {"editor_syntax_activation_for_java_files", test_editor_syntax_activation_for_java_files},
        {"editor_syntax_activation_for_csharp_files",
         test_editor_syntax_activation_for_csharp_files},
        {"editor_syntax_activation_for_haskell_files",
         test_editor_syntax_activation_for_haskell_files},
        {"editor_syntax_activation_for_ruby_files", test_editor_syntax_activation_for_ruby_files},
        {"editor_syntax_activation_for_ocaml_files", test_editor_syntax_activation_for_ocaml_files},
        {"editor_syntax_activation_for_markdown_files",
         test_editor_syntax_activation_for_markdown_files},
        {"editor_syntax_activation_for_toml_files", test_editor_syntax_activation_for_toml_files},
        {"editor_syntax_activation_for_yaml_files", test_editor_syntax_activation_for_yaml_files},
        {"editor_syntax_activation_for_xml_files", test_editor_syntax_activation_for_xml_files},
        {"editor_syntax_activation_for_make_files", test_editor_syntax_activation_for_make_files},
        {"editor_syntax_activation_for_diff_files", test_editor_syntax_activation_for_diff_files},
        {"editor_syntax_git_diff_tab_uses_source_language",
         test_editor_syntax_git_diff_tab_uses_source_language},
        {"editor_syntax_activation_for_julia_files", test_editor_syntax_activation_for_julia_files},
        {"editor_syntax_activation_for_scala_files", test_editor_syntax_activation_for_scala_files},
        {"editor_syntax_activation_for_ejs_files", test_editor_syntax_activation_for_ejs_files},
        {"editor_syntax_activation_for_erb_files", test_editor_syntax_activation_for_erb_files},
        {"editor_syntax_activation_for_regex_files", test_editor_syntax_activation_for_regex_files},
        {"editor_syntax_activation_for_latex_files", test_editor_syntax_activation_for_latex_files},
        {"editor_syntax_activation_for_go_and_mod_files",
         test_editor_syntax_activation_for_go_and_mod_files},
        {"editor_syntax_disabled_for_non_c_or_shell_files",
         test_editor_syntax_disabled_for_non_c_or_shell_files},
        {"editor_save_as_c_file_enables_syntax", test_editor_save_as_c_file_enables_syntax},
        {"editor_save_as_go_file_enables_syntax", test_editor_save_as_go_file_enables_syntax},
        {"editor_save_as_shell_and_non_shell_updates_syntax",
         test_editor_save_as_shell_and_non_shell_updates_syntax},
        {"editor_save_as_web_and_plain_updates_syntax",
         test_editor_save_as_web_and_plain_updates_syntax},
};

const int g_syntax_activation_test_count =
        (int)(sizeof(g_syntax_activation_tests) / sizeof(g_syntax_activation_tests[0]));
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "language/syntax.h"
#include "rotide.h"
#include "workspace/tabs.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

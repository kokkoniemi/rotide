;; Repo-local Clojure highlights.
;;
;; sogaiu/tree-sitter-clojure is a deliberately minimal, structural-only grammar:
;; its upstream highlights.scm colors only literals, comments, and quote
;; operators, leaving every symbol (including def/defn and call heads)
;; uncolored. This query keeps those literal captures and adds function-call and
;; special-form highlighting on top. RotIDE resolves overlapping captures
;; last-wins by query order, so broad patterns come first and the specific
;; overrides come later.

;; --- Literals -------------------------------------------------------------

(num_lit) @number

[
  (char_lit)
  (str_lit)
  (regex_lit)
] @string

[
  (bool_lit)
  (nil_lit)
] @constant.builtin

(kwd_lit) @constant

(comment) @comment

;; Reader macros and (un)quoting read as operators.
[
  "'"
  "`"
  "~"
  "@"
  "~@"
] @operator

;; --- Symbols in call position --------------------------------------------

;; The head symbol of a list / anonymous function is in call position.
(list_lit
  .
  (sym_lit name: (sym_name) @function))

(anon_fn_lit
  .
  (sym_lit name: (sym_name) @function))

;; The symbol a def-form introduces reads as a definition.
(list_lit
  .
  (sym_lit name: (sym_name) @_def)
  .
  (sym_lit name: (sym_name) @function)
  (#any-of? @_def
   "def" "defn" "defn-" "defmacro" "defmulti" "defmethod" "defprotocol"
   "defrecord" "deftype" "defstruct" "definterface" "defonce" "declare"))

;; --- Special forms and core macros ---------------------------------------

;; These override the call-position color wherever they appear as a symbol.
((sym_name) @keyword
 (#any-of? @keyword
  "def" "defn" "defn-" "defmacro" "defmulti" "defmethod" "defprotocol"
  "defrecord" "deftype" "defstruct" "definterface" "defonce" "declare"
  "ns" "in-ns" "require" "use" "import" "refer" "refer-clojure" "load"
  "let" "letfn" "binding" "loop" "recur" "fn" "if" "if-not" "if-let"
  "if-some" "when" "when-not" "when-let" "when-some" "when-first" "cond"
  "condp" "case" "do" "doto" "for" "doseq" "dotimes" "while" "quote"
  "var" "throw" "try" "catch" "finally" "monitor-enter" "monitor-exit"
  "new" "set!" "locking" "delay" "lazy-seq" "future"
  "and" "or" "not"
  "->" "->>" "some->" "some->>" "as->" "cond->" "cond->>"))

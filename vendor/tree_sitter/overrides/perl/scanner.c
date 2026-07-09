/* RotIDE minimal scanner for the reduced Perl highlight grammar.
 *
 * Replaces upstream tree-sitter-perl's 54-token scanner (quote-like operators,
 * autoquote lookahead, keyword intuition, error recovery synthetics). Only the
 * two jobs that genuinely need cross-line state survive:
 *
 *   - heredocs: `<<TERM` queues a terminator; at each following line start the
 *     body is emitted as _heredoc_text lines until the terminator line, which
 *     becomes heredoc_end (spanning exactly the terminator word, so RotIDE's
 *     heredoc-language injection can match it by name).
 *   - POD: a line-start `=word` swallows everything through the `=cut` line.
 *
 * Everything else the reduced grammar handles with ordinary regex tokens.
 */

#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum TokenType {
  HEREDOC_TOKEN,
  COMMAND_HEREDOC_TOKEN,
  HEREDOC_START,
  HEREDOC_TEXT,
  HEREDOC_END,
  POD,
};

#define MAX_PENDING 4
#define MAX_TERM 63

typedef struct {
  char term[MAX_TERM + 1];
  unsigned char len;
  unsigned char indented;
} Pending;

typedef struct {
  unsigned char count;
  Pending q[MAX_PENDING];
} Scanner;

static inline bool is_ident_start(int32_t c) {
  return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline bool is_ident_char(int32_t c) {
  return is_ident_start(c) || (c >= '0' && c <= '9');
}

static inline bool at_eol(int32_t c) { return c == '\n' || c == '\r' || c == 0; }

void *tree_sitter_perl_external_scanner_create(void) {
  return calloc(1, sizeof(Scanner));
}

void tree_sitter_perl_external_scanner_destroy(void *payload) { free(payload); }

unsigned tree_sitter_perl_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *s = (Scanner *)payload;
  unsigned size = sizeof(Scanner);
  if (size > TREE_SITTER_SERIALIZATION_BUFFER_SIZE) {
    size = TREE_SITTER_SERIALIZATION_BUFFER_SIZE;
  }
  memcpy(buffer, s, size);
  return size;
}

void tree_sitter_perl_external_scanner_deserialize(void *payload, const char *buffer,
                                                   unsigned length) {
  Scanner *s = (Scanner *)payload;
  memset(s, 0, sizeof(Scanner));
  if (length > 0) {
    if (length > sizeof(Scanner)) {
      length = sizeof(Scanner);
    }
    memcpy(s, buffer, length);
  }
}

/* Consume the rest of the current line, including the newline. */
static void consume_line(TSLexer *lexer) {
  while (!at_eol(lexer->lookahead) && !lexer->eof(lexer)) {
    lexer->advance(lexer, false);
  }
  if (lexer->lookahead == '\r') {
    lexer->advance(lexer, false);
  }
  if (lexer->lookahead == '\n') {
    lexer->advance(lexer, false);
  }
}

static bool scan_pod(TSLexer *lexer) {
  /* At column 0 on `=word`. Consume through the `=cut` line (or EOF). */
  lexer->advance(lexer, false); /* '=' */
  if (!is_ident_start(lexer->lookahead)) {
    return false;
  }
  consume_line(lexer);
  for (;;) {
    if (lexer->eof(lexer)) {
      break;
    }
    if (lexer->lookahead == '=') {
      /* Check for `=cut` at this line start. */
      static const char cut[] = "=cut";
      unsigned i = 0;
      bool matched = true;
      for (; i < 4; i++) {
        if (lexer->lookahead != (int32_t)cut[i]) {
          matched = false;
          break;
        }
        lexer->advance(lexer, false);
      }
      if (matched && !is_ident_char(lexer->lookahead)) {
        consume_line(lexer);
        break;
      }
    }
    consume_line(lexer);
  }
  lexer->mark_end(lexer);
  return true;
}

static bool scan_heredoc_token(TSLexer *lexer, Scanner *s, bool *is_command) {
  /* At `<<`. Claim it only when a valid heredoc delimiter follows. */
  lexer->advance(lexer, false); /* '<' */
  if (lexer->lookahead != '<') {
    return false;
  }
  lexer->advance(lexer, false); /* '<' */
  bool indented = false;
  if (lexer->lookahead == '~') {
    indented = true;
    lexer->advance(lexer, false);
  }
  int32_t quote = 0;
  if (lexer->lookahead == '"' || lexer->lookahead == '\'' || lexer->lookahead == '`') {
    quote = lexer->lookahead;
    lexer->advance(lexer, false);
  }
  if (!is_ident_start(lexer->lookahead)) {
    return false;
  }
  char term[MAX_TERM + 1];
  unsigned len = 0;
  while (is_ident_char(lexer->lookahead)) {
    if (len < MAX_TERM) {
      term[len++] = (char)lexer->lookahead;
    }
    lexer->advance(lexer, false);
  }
  term[len] = 0;
  if (quote != 0) {
    if (lexer->lookahead != quote) {
      return false;
    }
    lexer->advance(lexer, false);
  }
  lexer->mark_end(lexer);
  if (s->count < MAX_PENDING) {
    Pending *p = &s->q[s->count++];
    memcpy(p->term, term, len + 1);
    p->len = (unsigned char)len;
    p->indented = indented ? 1 : 0;
  }
  *is_command = (quote == '`');
  return true;
}

static bool scan_heredoc_body(TSLexer *lexer, Scanner *s, const bool *valid,
                              TSSymbol *result) {
  Pending *p = &s->q[0];
  /* For `<<~` heredocs the per-line indent is skipped (it is semantically
   * stripped anyway); this also lets heredoc_end span exactly the word. */
  if (p->indented) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
      lexer->advance(lexer, true);
    }
  }
  /* Try to match the terminator. Advancing past mark_end is allowed as pure
   * lookahead, so match-and-fallthrough into a text line is safe. */
  unsigned i = 0;
  while (i < p->len && lexer->lookahead == (int32_t)p->term[i]) {
    lexer->advance(lexer, false);
    i++;
  }
  bool is_end = false;
  if (i == p->len && !is_ident_char(lexer->lookahead)) {
    /* Terminator must be alone on its line. */
    if (at_eol(lexer->lookahead) || lexer->eof(lexer)) {
      is_end = true;
    }
  }
  if (is_end && valid[HEREDOC_END]) {
    lexer->mark_end(lexer);
    memmove(&s->q[0], &s->q[1], sizeof(Pending) * (unsigned)(s->count - 1));
    s->count--;
    *result = HEREDOC_END;
    return true;
  }
  if (!valid[HEREDOC_TEXT]) {
    return false;
  }
  consume_line(lexer);
  lexer->mark_end(lexer);
  *result = HEREDOC_TEXT;
  return true;
}

bool tree_sitter_perl_external_scanner_scan(void *payload, TSLexer *lexer,
                                            const bool *valid_symbols) {
  Scanner *s = (Scanner *)payload;

  /* Externals run before whitespace skipping. Skip blanks ourselves; a newline
   * with heredocs pending is where the body begins, marked by the zero-width
   * _heredoc_start that opens the heredoc_content extra. */
  bool at_line_start = lexer->get_column(lexer) == 0;
  for (;;) {
    if (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r') {
      lexer->advance(lexer, true);
      continue;
    }
    if (lexer->lookahead == '\n') {
      if (s->count > 0 && valid_symbols[HEREDOC_START]) {
        lexer->advance(lexer, true);
        lexer->mark_end(lexer);
        lexer->result_symbol = HEREDOC_START;
        return true;
      }
      lexer->advance(lexer, true);
      at_line_start = true;
      continue;
    }
    break;
  }

  /* Heredoc bodies and POD only ever start at column 0. */
  if (lexer->get_column(lexer) == 0 || at_line_start) {
    if (s->count > 0 && (valid_symbols[HEREDOC_TEXT] || valid_symbols[HEREDOC_END]) &&
        !lexer->eof(lexer)) {
      TSSymbol result;
      if (scan_heredoc_body(lexer, s, valid_symbols, &result)) {
        lexer->result_symbol = result;
        return true;
      }
      return false;
    }
    if (valid_symbols[POD] && lexer->lookahead == '=') {
      if (scan_pod(lexer)) {
        lexer->result_symbol = POD;
        return true;
      }
      return false;
    }
  }

  if ((valid_symbols[HEREDOC_TOKEN] || valid_symbols[COMMAND_HEREDOC_TOKEN]) &&
      lexer->lookahead == '<') {
    bool is_command = false;
    if (scan_heredoc_token(lexer, s, &is_command)) {
      lexer->result_symbol = is_command ? COMMAND_HEREDOC_TOKEN : HEREDOC_TOKEN;
      return true;
    }
    return false;
  }

  return false;
}

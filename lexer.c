#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <regex.h>
#include "lexer.h"

#define LINE_SIZE 1024
#define RE_SEP "[][(){};[:space:]]"

static char retok[] =
	"^(')."
	"|^(`)"
	"|^(,)"
	"|^(,@)"
	"|^(#')"
	"|^(#`)"
	"|^(#,)"
	"|^(#,@)"
	"|^(;.*)$"
	"|^([[({])"
	"|^([])}])"
	"|^(\\.)"RE_SEP
	"|^(#\\()"
	"|^(\".*\")"RE_SEP
	"|^(#t|#f)"RE_SEP
	"|^(-?[0-9]+\\.[0-9]+)"RE_SEP
	"|^(#[xX][0-9A-Fa-f]+)"RE_SEP
	"|^(-?[0-9]+)"RE_SEP
	"|^([^][(){};'`,[:space:]][^][(){};[:space:]]*)"RE_SEP;

static regmatch_t pmatch[tok_max] = {0};
static regex_t rexpr = {0};
static size_t off = 0;
static int line_number = 0;
static int column_number = 0;
static token tpeek = { .so = -1, .eo = -1};
static char *text;

static void strip_ws(void);

char *
tok_to_string(enum token t)
{
	switch (t) {
	case tok_any: return "any";
	case tok_quote: return "quote";
	case tok_quasiquote: return "quasiquote";
	case tok_unquote: return "unquote";
	case tok_unquote_splice: return "unquote-splice";
	case tok_syntax_quote: return "syntax-quote";
	case tok_syntax_quasiquote: return "syntax-quasiquote";
	case tok_syntax_unquote: return "syntax-unquote";
	case tok_syntax_unquote_splice: return "syntax-unquote-quasiquote";
	case tok_comment: return "comment";
	case tok_lbracket: return "lbracket";
	case tok_rbracket: return "rbracket";
	case tok_vector: return "vector";
	case tok_dot: return "dot";
	case tok_bool: return "bool";
	case tok_string: return "string";
	case tok_float: return "float";
	case tok_hex: return "hex";
	case tok_int: return "int";
	case tok_sym: return "sym";
	case tok_max: return "max";
	case tok_nomatch: return "nomatch";
	case tok_eof: return "eof";
	default: return NULL;
	}
}

static int
chin(int c, char *cstr)
{
	for (int i = 0; cstr[i] != '\0'; ++i) {
		if (c == cstr[i]) return 1;
	}
	return 0;
}

static void
strip_ws(void)
{
	for (; chin(text[off], " \t\v\r\f\n"); off++) {
		if (text[off] == '\n') {
			line_number++;
			column_number = 0;
		}
	}
}

token
next_token(void)
{
	token t = {
		.so = -1,
		.eo = -1,
		.ln = -1,
		.cn = -1,
	};
	if (tpeek.so != -1) {
		t = tpeek;
		tpeek = (token) {
			.so = -1,
			.eo = -1,
		};
		return t;
	}
	strip_ws();
	if (text[off] == '\0') {
		t.tag = tok_eof;
		return t;
	}
	int r = regexec(&rexpr, &text[off], tok_max, pmatch, 0);
	if (r) {
		if (r == REG_NOMATCH) {
			printf("reg nomatch (%d, %d)\n", line_number, column_number);
			t.tag = tok_nomatch;
		} else {
			assert(!"ERROR rematch");
		}
		return t;
	} else {
		int i;
		for (i = 1; i < tok_max; ++i) {
			if (pmatch[i].rm_so != -1) {
				break;
			}
		}
		if (i == tok_max) {
			printf("reg nomatch (%d, %d)\n", line_number, column_number);
			t.tag = tok_nomatch;
			return t;
		}
		t.tag = i;
		t.so = off;
		off += pmatch[i].rm_eo;
		t.eo = off;
		t.ln = line_number;
		t.cn = column_number;
		column_number += pmatch[i].rm_eo;
		return t;
	}
}

token
peek(void)
{
	if (tpeek.so == -1) {
		tpeek = next_token();
	}
	return tpeek;
}

int
lexer_init(char *str)
{
	text = str;
	char err_str[255];
	int r = regcomp(&rexpr, retok, REG_EXTENDED|REG_NEWLINE);
	if (r) {
		printf("(lexer_init) failed to init regex\n");
		regerror(r, &rexpr, err_str, sizeof(err_str));
		printf("%s\n", err_str);
		return 1;
	}
	off = 0;
	line_number = 0;
	column_number = 0;
	return 0;
}

void
lexer_end(void)
{
	regfree(&rexpr);
}

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <regex.h>
#include "common_def.h"
#include "lexer.h"
#include "sly_alloc.h"

#define LINE_SIZE 1024
#define RE_SEP "([][(){};[:space:]]|$)"

static char retok[] =
	"^(')."														// 1
	"|^(`)."													// 2
	"|^(,)."													// 3
	"|^(,@)."													// 4
	"|^(#')."													// 5
	"|^(#`)."													// 6
	"|^(#,)."													// 7
	"|^(#,@)."													// 8
	"|^(;.*)$"													// 9
	"|^([[({])"													// 10
	"|^([])}])"													// 11
	"|^(\\.)"RE_SEP												// 12
	"|^(#\\()"      										    // 13
	"|^(#uv8\\()"     										    // 14
	"|^(#dict\\()"     										    // 15
	"|^(λ|\\\\)"RE_SEP											// 16
	"|^(\"([^\"]|\\\\.)*\")"RE_SEP								// 17
	"|^(#t|#f)"RE_SEP											// 18
	"|^(-?[0-9]+\\.[0-9]+)"RE_SEP								// 19
	"|^(#[xX][0-9A-Fa-f]+)"RE_SEP								// 20
	"|^(-?[0-9]+)"RE_SEP										// 21
	"|^([^][(){};'`\"#,[:space:]][^][(){};[:space:]]*)"RE_SEP;	// 22

static regmatch_t pmatch[tok_max] = {0};
static regex_t rexpr = {0};
static size_t off = 0;
static int line_number = 0;
static int column_number = 0;
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
	case tok_byte_vector: return "byte-vector";
	case tok_dictionary: return "dictionary";
	case tok_dot: return "dot";
	case tok_bool: return "bool";
	case tok_lambda: return "λ";
	case tok_string: return "string";
	case tok_float: return "float";
	case tok_hex: return "hex";
	case tok_int: return "int";
	case tok_sym: return "sym";
	case tok_max: return "max";
	case tok_nomatch: return "nomatch";
	case tok_eof: return "eof";
	case tok__nocap0: return "nocapture0";
	case tok__nocap1: return "nocapture1";
	case tok__nocap2: return "nocapture2";
	case tok__nocap3: return "nocapture3";
	case tok__nocap4: return "nocapture4";
	case tok__nocap5: return "nocapture5";
	case tok__nocap6: return "nocapture6";
	case tok__nocap7: return "nocapture7";
	case tok__nocap8: return "nocapture8";
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
		.src = text,
	};
	strip_ws();
	if (text[off] == '\0') {
		t.tag = tok_eof;
		return t;
	}
	int r = regexec(&rexpr, &text[off], tok_max, pmatch, 0);
	if (r) {
		if (r == REG_NOMATCH) {
			printf("reg nomatch (%d, %d)\n", line_number, column_number);
			printf("%s\n", &text[off]);
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
			printf("%s\n", &text[off]);
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

static void
grow_token_buff(token_buff *tokens)
{
	if (tokens->max == 0) {
		tokens->max = 12;
	} else {
		tokens->max *= 2;
	}
	tokens->ts = realloc(tokens->ts, sizeof(token) * tokens->max);
}

void
push_token(token_buff *tokens, token t)
{
	if (tokens->len >= tokens->max) {
		grow_token_buff(tokens);
	}
	tokens->ts[tokens->len++] = t;
}

int
lex_str(char *str, token_buff *_tokens)
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
	if (text[0] == '#' && text[1] == '!') {
		while (text[off] != '\n' && text[off] != '\0') off++;
	}
	token_buff tokens = {0};
	token t = {0};
	do {
		t = next_token();
		push_token(&tokens, t);
	} while (t.tag != tok_eof);
	*_tokens = tokens;
	regfree(&rexpr);
	return 0;
}

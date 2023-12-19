#ifndef SLY_LEXER_H_
#define SLY_LEXER_H_

enum token {
	tok_any = 0,
	tok_quote,					// 1
	tok_quasiquote,				// 2
	tok_unquote_splice,			// 3
	tok_unquote,				// 4
	tok_syntax_quote,			// 5
	tok_syntax_quasiquote,		// 6
	tok_syntax_unquote_splice,	// 7
	tok_syntax_unquote,			// 8
	tok_comment,				// 9
	tok_lbracket,				// 10
	tok_rbracket,				// 11
	tok_dot,					// 12
	tok_vector,					// 13
	tok_byte_vector,			// 14
	tok_dictionary,				// 15
	tok_bool,					// 16
	tok_sexp_comment,
	tok_hex,
	tok_float,
	tok_int,
	tok_char,
	tok_keyword,
	tok_ident,
	tok_string,
	tok_max,
	tok_nomatch,
	tok_eof,
};

typedef struct _token {
	enum token tag;
	int so, eo;
	int ln, cn;
	char *src;
} token;

typedef struct {
	u32 cur;
	u32 len;
	size_t max;
	token *ts;
} token_buff;

#define PEEK_TOKEN(tb) ((tb).ts[(tb).cur])
#define NEXT_TOKEN(tb) ((tb).cur == (tb).len ? (tb).ts[(tb).cur-1] \
					                         : (tb).ts[(tb).cur++])

char *tok_to_string(enum token t);
int lex_str(char *str, token_buff *_tokens);
void push_token(token_buff *tokens, token t);

#endif /* SLY_LEXER_H_ */

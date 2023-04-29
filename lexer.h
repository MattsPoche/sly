#ifndef SLY_LEXER_H_
#define SLY_LEXER_H_

enum token {
	tok_any = 0,
	tok_quote,					// 1
	tok_quasiquote,				// 2
	tok_unquote,				// 3
	tok_unquote_splice,			// 4
	tok_syntax_quote,			// 5
	tok_syntax_quasiquote,		// 6
	tok_syntax_unquote,			// 7
	tok_syntax_unquote_splice,	// 8
	tok_comment,				// 9
	tok_lbracket,				// 10
	tok_rbracket,				// 11
	tok_dot,					// 12
	tok__nocap0,
	tok_vector,					// 13
	tok_byte_vector,			// 13
	tok_dictionary,				// 13
	tok_lambda,					// 14
	tok__nocap1,
	tok_string,					// 15
	tok__nocap2,
	tok__nocap3,	     		// 16
	tok_bool,					// 17
	tok__nocap4,
	tok_float,					// 18
	tok__nocap5,
	tok_hex,					// 19
	tok__nocap6,
	tok_int,					// 20
	tok__nocap7,
	tok_sym,					// 21
	tok__nocap8,
	tok_max,					// 22
	tok_nomatch,				// 23
	tok_eof,					// 24
};

typedef struct _token {
	enum token tag;
	int so, eo;
	int ln, cn;
} token;

typedef struct {
	size_t cur;
	size_t len;
	size_t max;
	token *ts;
} token_buff;

char *tok_to_string(enum token t);
int lex_str(char *str, token_buff *_tokens);

#endif /* SLY_LEXER_H_ */

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
	tok__nocap1,
	tok_string,					// 14
	tok__nocap2,
	tok__nocap3,	     		// 15
	tok_bool,					// 16
	tok__nocap4,
	tok_float,					// 17
	tok__nocap5,
	tok_hex,					// 18
	tok__nocap6,
	tok_int,					// 19
	tok__nocap7,
	tok_sym,					// 20
	tok__nocap8,
	tok_max,					// 21
	tok_nomatch,				// 22
	tok_eof,					// 23
};

typedef struct _token {
	enum token tag;
	int so, eo;
	int ln, cn;
} token;

char *tok_to_string(enum token t);
token next_token(void);
token peek(void);
int lexer_init(char *str);
void lexer_end(void);


#endif /* SLY_LEXER_H_ */

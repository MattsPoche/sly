#ifndef SLY_LEXER_H_
#define SLY_LEXER_H_

enum token {
	tok_any = 0,
	tok_quote,
	tok_quasiquote,
	tok_unquote,
	tok_unquote_splice,
	tok_syntax_quote,
	tok_syntax_quasiquote,
	tok_syntax_unquote,
	tok_syntax_unquote_splice,
	tok_comment,
	tok_lbracket,
	tok_rbracket,
	tok_dot,
	tok_vector,
	tok_string,
	tok_bool,
	tok_float,
	tok_hex,
	tok_int,
	tok_sym,
	tok_max,
	tok_nomatch,
	tok_eof,
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

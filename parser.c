#include "sly_types.h"
#include "lexer.h"
#include "parser.h"

static sly_value
parse_value(char *cstr, sly_value interned)
{
	token t = next_token();
	switch (t.tag) {
	case tok_any: {
		printf("(parse_value) any\n");
		sly_assert(0, "UNEMPLEMENTED");
	} break;
	case tok_quote: {
		sly_value stx = make_syntax(t, cstr_to_symbol("quote"));
		return cons(stx, cons(parse_value(cstr, interned), SLY_NULL));
	} break;
	case tok_quasiquote: {
		sly_value stx = make_syntax(t, cstr_to_symbol("quasiquote"));
		return cons(stx, cons(parse_value(cstr, interned), SLY_NULL));
	} break;
	case tok_unquote: {
		sly_value stx = make_syntax(t, cstr_to_symbol("unquote"));
		return cons(stx, cons(parse_value(cstr, interned), SLY_NULL));
	} break;
	case tok_unquote_splice: {
		sly_value stx = make_syntax(t, cstr_to_symbol("unquote-splice"));
		return cons(stx, cons(parse_value(cstr, interned), SLY_NULL));
	} break;
	case tok_syntax_quote: {
		sly_value stx = make_syntax(t, cstr_to_symbol("syntax-unquote"));
		return cons(stx, cons(parse_value(cstr, interned), SLY_NULL));
	} break;
	case tok_syntax_quasiquote: {
		sly_value stx = make_syntax(t, cstr_to_symbol("syntax-quasiquote"));
		return cons(stx, cons(parse_value(cstr, interned), SLY_NULL));
	} break;
	case tok_syntax_unquote: {
		sly_value stx = make_syntax(t, cstr_to_symbol("syntax-unquote"));
		return cons(stx, cons(parse_value(cstr, interned), SLY_NULL));
	} break;
	case tok_syntax_unquote_splice: {
		sly_value stx = make_syntax(t, cstr_to_symbol("syntax-unquote-splice"));
		return cons(stx, cons(parse_value(cstr, interned), SLY_NULL));
	} break;
	case tok_comment: {
		return parse_value(cstr, interned);
	} break;
	case tok_lbracket: {
		if (peek().tag == tok_rbracket) return SLY_NULL;
		sly_value list = cons(parse_value(cstr, interned), SLY_NULL);
		for (;;) {
			t = peek();
			if (t.tag == tok_rbracket) {
				next_token();
				return list;
			}
			if (t.tag == tok_dot) {
				next_token();
				append(list, parse_value(cstr, interned));
				sly_assert(next_token().tag == tok_rbracket, "Parse Error expected closing bracket");
				return list;
			}
			append(list, cons(parse_value(cstr, interned), SLY_NULL));
		}
	} break;
	case tok_rbracket: {
		sly_assert(0, "Parse Error mismatched brackets");
	} break;
	case tok_vector: {
		sly_assert(0, "Parse Error Unemplemented");
	} break;
	case tok_dot: {
		sly_assert(0, "Parse Error bad dot");
	} break;
	case tok_string: {
		return make_syntax(t, make_string(&cstr[t.so+1], t.eo - t.so - 2));
	} break;
	case tok_bool: {
		if (cstr[t.so+1] == 'f') {
			return make_syntax(t, SLY_FALSE);
		} else {
			return make_syntax(t, SLY_TRUE);
		}
	} break;
	case tok_float: {
		return make_syntax(t, make_float(strtod(&cstr[t.so], NULL)));
	} break;
	case tok_hex: {
		return make_syntax(t, make_int(strtol(&cstr[t.so+2], NULL, 16)));
	} break;
	case tok_int: {
		return make_syntax(t, make_int(strtol(&cstr[t.so], NULL, 0)));
	} break;
	case tok_sym: {
		size_t len = t.eo - t.so;
		sly_value stx = make_syntax(t, make_symbol(interned, &cstr[t.so], len));
		return stx;
	} break;
	case tok_max: {
		printf("(parse_value) max\n");
		sly_assert(0, "UNEMPLEMENTED");
	} break;
	case tok_nomatch: {
		printf("(parse_value) nomatch %d, %d\n", t.so, t.eo);
		sly_assert(0, "UNEMPLEMENTED");
	} break;
	case tok_eof: {
	} break;
	}
	return SLY_NULL;
}

sly_value
parse(char *cstr, sly_value interned)
{
	lexer_init(cstr);

	sly_value code = cons(make_syntax((token){}, cstr_to_symbol("begin")),
						  SLY_NULL);
	sly_value val;
	while (peek().tag != tok_eof) {
		if (null_p(val = parse_value(cstr, interned))) break;
		append(code, cons(val, SLY_NULL));
	}
	lexer_end();
	return code;
}

static size_t
get_file_size(FILE *file)
{
	fseek(file, 0, SEEK_END);
	size_t size = ftell(file);
	fseek(file, 0, SEEK_SET);
	return size;
}

sly_value
parse_file(char *file_path, char **contents, sly_value interned)
{
	FILE *file = fopen(file_path, "r");
	size_t size = get_file_size(file);
	char *str = sly_alloc(size+1);
	fread(str, 1, size, file);
	str[size] = '\0';
	fclose(file);
	*contents = str;
	return parse(str, interned);
}

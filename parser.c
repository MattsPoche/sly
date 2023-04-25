#include <assert.h>
#include <errno.h>
#include "sly_types.h"
#include "lexer.h"
#include "parser.h"
#include "sly_alloc.h"

static char *
escape_string(Sly_State *ss, char *str, size_t len)
{
	char *buf = MALLOC(len+1);
	assert(buf != NULL);
	size_t i, j;
	for (i = 0, j = 0; i < len; ++i, ++j) {
		if (str[i] == '\\') {
			i++;
			switch (str[i]) {
			case 'a': {
				buf[j] = '\a';
			} break;
			case 'b': {
				buf[j] = '\b';
			} break;
			case 'f': {
				buf[j] = '\f';
			} break;
			case 'n': {
				buf[j] = '\n';
			} break;
			case 'r': {
				buf[j] = '\r';
			} break;
			case 't': {
				buf[j] = '\t';
			} break;
			case 'v': {
				buf[j] = '\v';
			} break;
			case '\\': {
				buf[j] = '\\';
			} break;
			case '\'': {
				buf[j] = '\'';
			} break;
			case '"': {
				buf[j] = '"';
			} break;
			default: {
				sly_raise_exception(ss, EXC_COMPILE, "Parse Error unrecognized escape sequence");
			} break;
			}
		} else {
			buf[j] = str[i];
		}
	}
	buf[j] = '\0';
	return buf;
}

static sly_value
parse_value(Sly_State *ss, char *cstr)
{
	token t = next_token();
	sly_value list;
	switch (t.tag) {
	case tok_any: {
		printf("(parse_value) any\n");
		sly_assert(0, "(parse_value, tok_any)UNEMPLEMENTED");
	} break;
	case tok_quote: {
		sly_value stx = make_syntax(ss, t, cstr_to_symbol("quote"));
		return cons(ss, stx, cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_quasiquote: {
		sly_value stx = make_syntax(ss, t, cstr_to_symbol("quasiquote"));
		return cons(ss, stx, cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_unquote: {
		sly_value stx = make_syntax(ss, t, cstr_to_symbol("unquote"));
		return cons(ss, stx, cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_unquote_splice: {
		sly_value stx = make_syntax(ss, t, cstr_to_symbol("unquote-splice"));
		return cons(ss, stx, cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_syntax_quote: {
		sly_value stx = make_syntax(ss, t, cstr_to_symbol("syntax-quote"));
		return cons(ss, stx, cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_syntax_quasiquote: {
		sly_value stx = make_syntax(ss, t, cstr_to_symbol("syntax-quasiquote"));
		return cons(ss, stx, cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_syntax_unquote: {
		sly_value stx = make_syntax(ss, t, cstr_to_symbol("syntax-unquote"));
		return cons(ss, stx, cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_syntax_unquote_splice: {
		sly_value stx = make_syntax(ss, t, cstr_to_symbol("syntax-unquote-splice"));
		return cons(ss, stx, cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_comment: {
		return parse_value(ss, cstr);
	} break;
	case tok_lbracket: {
		if (peek().tag == tok_rbracket) {
			next_token();
			return SLY_NULL;
		}
		list = cons(ss, parse_value(ss, cstr), SLY_NULL);
build_list:
		for (;;) {
			t = peek();
			if (t.tag == tok_eof) {
				sly_raise_exception(ss, EXC_COMPILE, "Parse Error unexpected end of file");
			}
			if (t.tag == tok_rbracket) {
				next_token();
				return list;
			}
			if (t.tag == tok_dot) {
				next_token();
				append(list, parse_value(ss, cstr));
				if (next_token().tag != tok_rbracket) {
					sly_raise_exception(ss, EXC_COMPILE, "Parse Error expected closing bracket");
				}
				return list;
			}
			append(list, cons(ss, parse_value(ss, cstr), SLY_NULL));
		}
	} break;
	case tok_rbracket: {
		printf("DEBUG:\n%s\n", &cstr[t.so]);
		sly_raise_exception(ss, EXC_COMPILE, "Parse Error mismatched bracket");
	} break;
	case tok_vector: {
		sly_value stx = make_syntax(ss, t, cstr_to_symbol("make-vector"));
		list = cons(ss, stx, SLY_NULL);
		goto build_list;
	} break;
	case tok_dot: {
		sly_raise_exception(ss, EXC_COMPILE, "Parse Error bad dot");
	} break;
	case tok_lambda: {
		return make_syntax(ss, t, cstr_to_symbol("lambda"));
	} break;
	case tok_string: {
		char *s = escape_string(ss, &cstr[t.so+1], t.eo - t.so - 2);
		sly_value stx = make_syntax(ss, t, make_string(ss, s, strlen(s)));
		FREE(s);
		return stx;
	} break;
	case tok_bool: {
		if (cstr[t.so+1] == 'f') {
			return make_syntax(ss, t, SLY_FALSE);
		} else {
			return make_syntax(ss, t, SLY_TRUE);
		}
	} break;
	case tok_float: {
		return make_syntax(ss, t, make_float(ss, strtod(&cstr[t.so], NULL)));
	} break;
	case tok_hex: {
		return make_syntax(ss, t, make_int(ss, strtol(&cstr[t.so+2], NULL, 16)));
	} break;
	case tok_int: {
		return make_syntax(ss, t, make_int(ss, strtol(&cstr[t.so], NULL, 0)));
	} break;
	case tok_sym: {
		size_t len = t.eo - t.so;
		sly_value stx = make_syntax(ss, t, make_symbol(ss, &cstr[t.so], len));
		return stx;
	} break;
	case tok_max: {
		printf("(parse_value) max\n");
		sly_assert(0, "(parse_value, tok_max) UNEMPLEMENTED");
	} break;
	case tok_nomatch: {
		printf("(parse_value) nomatch %d, %d\n", t.so, t.eo);
		sly_assert(0, "(parse_value, tok_nomatch) UNEMPLEMENTED");
	} break;
	case tok_eof: {
	} break;
	case tok__nocap0:
	case tok__nocap1:
	case tok__nocap2:
	case tok__nocap3:
	case tok__nocap4:
	case tok__nocap5:
	case tok__nocap6:
	case tok__nocap7:
	case tok__nocap8: {
		sly_raise_exception(ss, EXC_COMPILE, "Parse Error undefined token (nocapture0)");
	} break;
	}
	return SLY_NULL;
}

sly_value
parse(Sly_State *ss, char *cstr)
{
	lexer_init(cstr);

	sly_value code = cons(ss, make_syntax(ss, (token){0}, cstr_to_symbol("begin")),
						  SLY_NULL);
	sly_value val;
	while (peek().tag != tok_eof) {
		if (null_p(val = parse_value(ss, cstr))) break;
		append(code, cons(ss, val, SLY_NULL));
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
parse_file(Sly_State *ss, char *file_path, char **contents)
{
	FILE *file = fopen(file_path, "r");
	if (file == NULL) {
		fprintf(stderr, "%s\n", strerror(errno));
		sly_raise_exception(ss, EXC_COMPILE, "Error unable to open source file");
	}
	size_t size = get_file_size(file);
	char *str = MALLOC(size+1);
	assert(str != NULL);
	fread(str, 1, size, file);
	str[size] = '\0';
	fclose(file);
	*contents = str;
	return parse(ss, str);
}

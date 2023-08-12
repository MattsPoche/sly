#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#include "sly_types.h"
#include "lexer.h"
#include "parser.h"
#include "sly_alloc.h"

#define syntax_cons(car, cdr) make_syntax(ss, t, cons(ss, car, cdr))
#define peek() PEEK_TOKEN(tokens)
#define next_token() NEXT_TOKEN(tokens)

static token_buff tokens;

static i8
parse_char(Sly_State *ss, char *str, size_t len)
{
	if (len == 3) {
		return str[2];
	}
	if (isdigit(str[2])) {
		long i = strtol(&str[2], NULL, 8);
		if (i > UCHAR_MAX || i < CHAR_MIN) {
			sly_raise_exception(ss, EXC_COMPILE, "Parse Error byte value out of range");
		}
		return i;
	}
	if (str[2] == 'x') {
		long i = strtol(&str[3], NULL, 16);
		if (i > UCHAR_MAX || i < CHAR_MIN) {
			sly_raise_exception(ss, EXC_COMPILE, "Parse Error byte value out of range");
		}
		return i;
	}
	if (strncmp(str, "#\\nul", len) == 0) {
		return 0x0;
	} else if (strncmp(str, "#\\alarm", len) == 0) {
		return 0x7;
	} else if (strncmp(str, "#\\backspace", len) == 0) {
		return 0x8;
	} else if (strncmp(str, "#\\tab", len) == 0) {
		return 0x9;
	} else if (strncmp(str, "#\\newline", len) == 0
			   || strncmp(str, "#\\linefeed", len) == 0) {
		return 0xa;
	} else if (strncmp(str, "#\\vtab", len) == 0) {
		return 0xb;
	} else if (strncmp(str, "#\\page", len) == 0) {
		return 0xc;
	} else if (strncmp(str, "#\\return", len) == 0) {
		return 0xd;
	} else if (strncmp(str, "#\\esc", len) == 0
			   || strncmp(str, "#\\escape", len) == 0) {
		return 0x1b;
	} else if (strncmp(str, "#\\space", len) == 0) {
		return 0x20;
	} else if (strncmp(str, "#\\delete", len) == 0) {
		return 0x7f;
	}
	sly_raise_exception(ss, EXC_COMPILE, "Parse Error unrecognized char literal");
	return '\0';
}

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
		return syntax_cons(make_syntax(ss, t, cstr_to_symbol("quote")),
						   cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_quasiquote: {
		return syntax_cons(make_syntax(ss, t, cstr_to_symbol("quasiquote")),
						   cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_unquote: {
		return syntax_cons(make_syntax(ss, t, cstr_to_symbol("unquote")),
						   cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_unquote_splice: {
		return syntax_cons(make_syntax(ss, t, cstr_to_symbol("unquote-splice")),
						   cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_syntax_quote: {
		return syntax_cons(make_syntax(ss, t, cstr_to_symbol("syntax-quote")),
						   cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_syntax_quasiquote: {
		return syntax_cons(make_syntax(ss, t, cstr_to_symbol("syntax-quasiquote")),
						   cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_syntax_unquote: {
		return syntax_cons(make_syntax(ss, t, cstr_to_symbol("syntax-unquote")),
						   cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_syntax_unquote_splice: {
		return syntax_cons(make_syntax(ss, t, cstr_to_symbol("syntax-unquote-splice")),
						   cons(ss, parse_value(ss, cstr), SLY_NULL));
	} break;
	case tok_comment: {
		return parse_value(ss, cstr);
	} break;
	case tok_lbracket: {
		token start_list;
		if (peek().tag == tok_rbracket) {
			next_token();
			return SLY_NULL;
		}
		list = cons(ss, parse_value(ss, cstr), SLY_NULL);
build_list:
		start_list = t;
		for (;;) {
			t = peek();
			if (t.tag == tok_eof) {
				sly_display(list, 1);
				printf("\n");
				sly_raise_exception(ss, EXC_COMPILE, "Parse Error unexpected end of file");
			}
			if (t.tag == tok_rbracket) {
				next_token();
				break;
			}
			if (t.tag == tok_dot) {
				next_token();
				append(list, parse_value(ss, cstr));
				if (next_token().tag != tok_rbracket) {
					sly_raise_exception(ss, EXC_COMPILE, "Parse Error expected closing bracket");
				}
				break;
			}
			append(list, cons(ss, parse_value(ss, cstr), SLY_NULL));
		}
		return make_syntax(ss, start_list, list);
	} break;
	case tok_rbracket: {
		printf("DEBUG:%d:%d\n%s\n", t.ln, t.cn, cstr);
		sly_raise_exception(ss, EXC_COMPILE, "Parse Error mismatched bracket");
	} break;
	case tok_vector: {
		sly_value stx = make_syntax(ss, t, cstr_to_symbol("vector"));
		list = cons(ss, stx, SLY_NULL);
		goto build_list;
	} break;
	case tok_byte_vector: {
		sly_value stx = make_syntax(ss, t, cstr_to_symbol("make-byte-vector"));
		list = cons(ss, stx, SLY_NULL);
		goto build_list;
	} break;
	case tok_dictionary: {
		sly_value stx = make_syntax(ss, t, cstr_to_symbol("make-dictionary"));
		list = cons(ss, stx, SLY_NULL);
		goto build_list;
	} break;
	case tok_dot: {
		sly_raise_exception(ss, EXC_COMPILE, "Parse Error bad dot");
	} break;
	case tok_char: {
		return make_syntax(ss, t, make_byte(ss, parse_char(ss, &cstr[t.so], t.eo - t.so)));
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
	case tok_keyword: {
		return syntax_cons(make_syntax(ss, t, cstr_to_symbol("quote")),
						   make_syntax(ss, t, make_symbol(ss, &cstr[t.so], t.eo - t.so)));
	} break;
	case tok_ident: {
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
	}
	return SLY_NULL;
}

sly_value
parse(Sly_State *ss, char *cstr)
{
	tokens = (token_buff){0};
	lex_str(cstr, &tokens);
	sly_value code = cons(ss,
						  make_syntax(ss, (token){0}, cstr_to_symbol("begin")),
						  SLY_NULL);
	sly_value val;
	while (peek().tag != tok_eof) {
		if (null_p(val = parse_value(ss, cstr))) break;
		append(code, cons(ss, val, SLY_NULL));
	}
	free(tokens.ts);
	return make_syntax(ss, (token){0}, code);
}

static size_t
get_file_size(FILE *file)
{
	fseek(file, 0, SEEK_END);
	size_t size = ftell(file);
	fseek(file, 0, SEEK_SET);
	return size;
}

char *
cat_files(int num_files, ...)
{
	va_list ap;
	char *file_name;
	char *buffer;
	size_t total_size;
	va_start(ap, num_files);
	FILE *mem = open_memstream(&buffer, &total_size);
	while (num_files--) {
		file_name = va_arg(ap, char *);
		FILE *f = fopen(file_name, "r");
		int c;
		fprintf(mem, ";;; %s\n\n", file_name);
		while ((c = fgetc(f)) != EOF) {
			fputc(c, mem);
		}
		fputc('\n', mem);
		fclose(f);
	}
	fclose(mem);
	va_end(ap);
	return buffer;
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

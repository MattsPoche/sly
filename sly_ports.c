#include <stdio.h>
#include <string.h>
#include "sly_types.h"
#include "sly_ports.h"

#define EOF_OBJECT(ss) dictionary_ref((ss)->cc->globals, cstr_to_symbol("eof"))

struct output_string {
	FILE *stream;
	char *str;
	size_t size;
};

int
input_port_p(Sly_State *ss, sly_value port)
{
	if (!user_data_p(port)) {
		return 0;
	}
	sly_value plist = user_data_get_properties(port);
	sly_value v = plist_get(plist, cstr_to_symbol("type:"));
	return sly_eq(v, cstr_to_symbol("input-port"));
}

int
output_port_p(Sly_State *ss, sly_value port)
{
	if (!user_data_p(port)) {
		return 0;
	}
	sly_value plist = user_data_get_properties(port);
	sly_value v = plist_get(plist, cstr_to_symbol("type:"));
	return sly_eq(v, cstr_to_symbol("output-port"));
}

int
port_p(Sly_State *ss, sly_value port)
{
	if (!user_data_p(port)) {
		return 0;
	}
	sly_value plist = user_data_get_properties(port);
	sly_value prop = cstr_to_symbol("type:");
	return sly_eq(plist_get(plist, prop), cstr_to_symbol("output-port"))
		|| sly_eq(plist_get(plist, prop), cstr_to_symbol("input-port"));
}

int
string_port_p(Sly_State *ss, sly_value port)
{
	if (port_p(ss, port)) {
		sly_value plist = user_data_get_properties(port);
		return sly_eq(plist_get(plist, cstr_to_symbol("port-type:")),
					  cstr_to_symbol("file-stream-port"));
	} else {
		return 0;
	}
}

int
port_closed_p(Sly_State *ss, sly_value port)
{
	return port_p(ss, port)
		&& user_data_get(port) == NULL;
}

int
file_stream_port_p(Sly_State *ss, sly_value port)
{
	if (port_p(ss, port)) {
		sly_value plist = user_data_get_properties(port);
		return string_p(plist_get(plist, cstr_to_symbol("file-path:")));
	} else {
		return 0;
	}
}

int
eof_object_p(Sly_State *ss, sly_value v)
{
	return sly_eq(v, EOF_OBJECT(ss));
}

void
port_set_stream(sly_value port, FILE *stream)
{
	user_data_set(port, &stream);
}

FILE *
port_get_stream(sly_value port)
{
	FILE **f = user_data_get(port);
	return *f;
}

sly_value
make_input_port(Sly_State *ss)
{
	sly_value port = make_user_data(ss, sizeof(FILE *));
	sly_value plist = SLY_NULL;
	plist = plist_put(ss, plist,
					  cstr_to_symbol("type:"),
					  cstr_to_symbol("input-port"));
	user_data_set_properties(port, plist);
	return port;
}

sly_value
make_output_port(Sly_State *ss)
{
	sly_value port = make_user_data(ss, sizeof(FILE *));
	sly_value plist = SLY_NULL;
	plist = plist_put(ss, plist,
					  cstr_to_symbol("type:"),
					  cstr_to_symbol("output-port"));
	user_data_set_properties(port, plist);
	return port;
}

sly_value
open_input_file(Sly_State *ss, sly_value file_path)
{
	sly_value port = make_input_port(ss);
	sly_value plist = user_data_get_properties(port);
	plist = plist_put(ss, plist,
					  cstr_to_symbol("file-path:"),
					  file_path);
	plist = plist_put(ss, plist,
					  cstr_to_symbol("port-type:"),
					  cstr_to_symbol("file-stream-port"));
	user_data_set_properties(port, plist);
	char *str = string_to_cstr(file_path);
	FILE *f = fopen(str, "r");
	port_set_stream(port, f);
	return port;
}

sly_value
open_output_string(Sly_State *ss)
{
	sly_value port = make_user_data(ss, sizeof(struct output_string));
	sly_value plist = user_data_get_properties(port);
	plist = plist_put(ss, plist,
					  cstr_to_symbol("type:"),
					  cstr_to_symbol("output-port"));
	plist = plist_put(ss, plist,
					  cstr_to_symbol("port-type:"),
					  cstr_to_symbol("string-port"));
	user_data_set_properties(port, plist);
	struct output_string *data = user_data_get(port);
	data->stream = open_memstream(&data->str, &data->size);
	return port;
}

sly_value
open_output_file(Sly_State *ss, sly_value file_path, int append)
{
	sly_value port = make_output_port(ss);
	sly_value plist = user_data_get_properties(port);
	plist = plist_put(ss, plist,
					  cstr_to_symbol("file-path:"),
					  file_path);
	plist = plist_put(ss, plist,
					  cstr_to_symbol("port-type:"),
					  cstr_to_symbol("file-stream-port"));
	user_data_set_properties(port, plist);
	char *str = string_to_cstr(file_path);
	FILE *f;
	if (append) {
		f = fopen(str, "a");
	} else {
		f = fopen(str, "w");
	}
	port_set_stream(port, f);
	return port;
}


void
flush_output(Sly_State *ss, sly_value port)
{
	sly_assert(output_port_p(ss, port), "Type Error expected output-port");
	fflush(port_get_stream(port));
}

i64
file_position(Sly_State *ss, sly_value port, i64 pos)
{
	sly_assert(port_p(ss, port), "Type Error expected port");
	if (pos < 0) {
		return ftell(port_get_stream(port));
	}
	fseek(port_get_stream(port), pos, SEEK_SET);
	return pos;
}

sly_value
get_output_string(Sly_State *ss, sly_value port)
{
	sly_assert(string_port_p(ss, port), "Type Error expected string-port");
	struct output_string *data = user_data_get(port);
	fclose(data->stream);
	data->stream = NULL;
	return make_string(ss, data->str, data->size);
}

void
close_input_port(Sly_State *ss, sly_value port)
{
	sly_assert(input_port_p(ss, port), "Type Error expected input-port");
	fclose(port_get_stream(port));
	port_set_stream(port, NULL);
}

void
close_output_port(Sly_State *ss, sly_value port)
{
	sly_assert(output_port_p(ss, port), "Type Error expected output-port");
	fclose(port_get_stream(port));
	port_set_stream(port, NULL);
}

void
write_char(Sly_State *ss, sly_value ch, sly_value port)
{
	sly_assert(byte_p(ch), "Type Error expected char");
	sly_assert(output_port_p(ss, port), "Type Error expected output-port");
	fputc(get_byte(ch), port_get_stream(port));
}

i64
write_string(Sly_State *ss, sly_value str, sly_value port, i64 start, i64 end)
{
	sly_assert(output_port_p(ss, port), "Type Error expected output-port");
	sly_assert(string_p(str), "Type Error expected output-port");
	i64 len = end - start;
	byte_vector *ptr = GET_PTR(str);
	return fwrite(&ptr->elems[start], 1, len, port_get_stream(port));
}

sly_value
read_char(Sly_State *ss, sly_value port)
{
	sly_assert(input_port_p(ss, port), "Type Error expected input-port");
	int ch = fgetc(port_get_stream(port));
	if (ch == EOF) {
		return EOF_OBJECT(ss);
	} else {
		return make_byte(ss, ch);
	}
}

sly_value
read_string(Sly_State *ss, sly_value port, size_t len)
{
	sly_assert(input_port_p(ss, port), "Type Error expected input-port");
	sly_value s = make_byte_vector(ss, len, len);
	byte_vector *ptr = GET_PTR(s);
	ptr->h.type = tt_string;
	ptr->len = fread(ptr->elems, 1, len, port_get_stream(port));
	return s;
}

sly_value
read_line(Sly_State *ss, sly_value port)
{
	static char buf[1024];
	char *str;
	sly_assert(input_port_p(ss, port), "Type Error expected input-port");
	str = fgets(buf, sizeof(buf), port_get_stream(port));
	if (str == NULL) {
		return EOF_OBJECT(ss);
	}
	size_t len = strlen(str);
	if (len > 0 && str[len-1] == '\n') len--;
	return make_string(ss, str, len);
}

sly_value
port_to_lines(Sly_State *ss, sly_value port)
{
	sly_assert(input_port_p(ss, port), "Type Error expected input-port");
	sly_value line = read_line(ss, port);
	sly_value eof = EOF_OBJECT(ss);
	if (sly_eq(line, eof)) {
		close_input_port(ss, port);
		return line;
	}
	sly_value list = cons(ss, line, SLY_NULL);
	sly_value tail = list;
	while (!sly_eq(line = read_line(ss, port), eof)) {
		sly_value tmp = cons(ss, line, SLY_NULL);
		set_cdr(tail, tmp);
		tail = tmp;
	}
	close_input_port(ss, port);
	return list;
}

sly_value
port_to_string(Sly_State *ss, sly_value port)
{
	sly_assert(input_port_p(ss, port), "Type Error expected input-port");
	FILE *f = port_get_stream(port);
	fseek(f, 0, SEEK_END);
	size_t len = ftell(f);
	rewind(f);
	sly_value str = make_byte_vector(ss, len, len);
	byte_vector *s = GET_PTR(str);
	s->h.type = tt_string;
	fread(s->elems, 1, len, f);
	close_input_port(ss, port);
	return str;
}

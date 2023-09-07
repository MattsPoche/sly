#ifndef SLY_PORTS_H_
#define SLY_PORTS_H_

int eof_object_p(Sly_State *ss, sly_value v);
int input_port_p(Sly_State *ss, sly_value port);
int output_port_p(Sly_State *ss, sly_value port);
int port_p(Sly_State *ss, sly_value port);
int string_port_p(Sly_State *ss, sly_value port);
int file_stream_port_p(Sly_State *ss, sly_value port);
int port_closed_p(Sly_State *ss, sly_value port);
void port_set_stream(sly_value port, FILE *stream);
FILE *port_get_stream(sly_value port);
sly_value make_input_port(Sly_State *ss);
sly_value make_output_port(Sly_State *ss);
sly_value open_input_file(Sly_State *ss, sly_value file_path);
sly_value open_output_file(Sly_State *ss, sly_value file_path, int append);
void close_input_port(Sly_State *ss, sly_value port);
void close_output_port(Sly_State *ss, sly_value port);
sly_value open_output_string(Sly_State *ss);
sly_value get_output_string(Sly_State *ss, sly_value port);
void flush_output(Sly_State *ss, sly_value port);
i64 file_position(Sly_State *ss, sly_value port, i64 pos);
void write_char(Sly_State *ss, sly_value ch, sly_value port);
i64 write_string(Sly_State *ss, sly_value str, sly_value port, i64 start, i64 end);
sly_value read_char(Sly_State *ss, sly_value port);
sly_value read_string(Sly_State *ss, sly_value port, size_t len);
sly_value read_line(Sly_State *ss, sly_value port);
sly_value port_to_string(Sly_State *ss, sly_value port);
sly_value port_to_lines(Sly_State *ss, sly_value port);

#endif /* SLY_PORTS_H_ */

#ifndef SLY_PARSER_H_
#define SLY_PARSER_H_

sly_value parse(char *cstr, sly_value interned);
sly_value parse_file(char *file_path, char **contents, sly_value interned);

#endif /* SLY_PARSER_H_ */

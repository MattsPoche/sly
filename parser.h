#ifndef SLY_PARSER_H_
#define SLY_PARSER_H_

char *cat_files(int num_files, ...);
sly_value parse(Sly_State *ss, char *cstr);
sly_value parse_file(Sly_State *ss, char *file_path, char **contents);

#endif /* SLY_PARSER_H_ */

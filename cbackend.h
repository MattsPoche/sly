#ifndef CBACKEND_H_
#define CBACKEND_H_

void cps_emit_c(Sly_State *ss, sly_value graph,
				sly_value k, sly_value free_vars, FILE *file);

#endif /* CBACKEND_H_ */

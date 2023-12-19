#ifndef SLY_EXPANDER_
#define SLY_EXPANDER_

void sly_expand_init(Sly_State *ss, sly_value env);
sly_value sly_expand(Sly_State *ss, sly_value env, sly_value ast);

#endif /* SLY_EXPANDER_ */

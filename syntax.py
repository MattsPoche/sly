import re

def str_to_list(s):
	return eval(re.sub(r'([^][{}()^\s]+)', r'"\1",', s)
				.replace("(", "[")
				.replace(")", "], "))[0]

def car(xs):
	assert isinstance(xs, list)
	return xs[0]

def cdr(xs):
	assert isinstance(xs, list)
	return xs[1:]

def match(form, literals, pattern):
	def _match(form, literals, pattern, pvars):
		if pvars is None:
			return False
		if isinstance(pattern, str):
			if pattern in literals:
				if form in literals:
					return True
				else:
					return False
			p = pvars.get(pattern)
			if p:
				p.append(form)
			else:
				pvars[pattern] = [form]
		elif isinstance(pattern, list):
			if pattern and form:
				if len(pattern) > 1 and car(cdr(pattern)) == '...':
					if isinstance(form, list):
						while form:
							_match(car(form), literals, car(pattern), pvars)
							form = cdr(form)
						pattern = cdr(cdr(pattern))
					else:
						return False
				else:
					return _match(car(form), literals, car(pattern), pvars) \
				       and _match(cdr(form), literals, cdr(pattern), pvars)
		return True
	pvars = {}
	if _match(form, literals, pattern, pvars):
		return pvars
	else:
		return None

def expand(pvars, template):
	if isinstance(template, str):
		v = pvars.get(template)
		if v is None:
			return template
		else:
			return car(v)
	elif template and isinstance(template, list):
		if len(template) > 1 and car(cdr(template)) == '...':
			return pvars.get(car(template)) + expand(pvars, cdr(cdr(template)))
		else:
			return [expand(pvars, car(template))] + expand(pvars, cdr(template))
	return template

def syntax_expand(form, literals, *clauses):
	for clause in clauses:
		pattern, template = clause
		pvars = match(cdr(form), literals, cdr(pattern))
		if pvars:
			return expand(pvars, template)

src = '''
((let ((x 1)
       (y 2))
  (+ x y)
  (foo))
()
((let ((var val) ...) body ...)
 ((lambda (var ...) body ...) val ...))
((let label ((var val) ...) body ...)
 ((lambda ()
    (define (label var ...) body ...)
    (label val ...)))))
'''

def list_print(xs):
	if len(xs) == 0:
		print('()', end=' ')
		return
	print('(', sep='', end='')
	for x in xs:
		if isinstance(x, list):
			list_print(x)
		else:
			print(x, end=' ')
	print('\b)', end=' ')

def list_println(xs):
	list_print(xs)
	print()

if __name__ == '__main__':
	expr = str_to_list(src)
	list_println(syntax_expand(*expr))

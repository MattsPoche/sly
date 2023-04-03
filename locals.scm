(define foo
  (lambda ()
	(define x 69)
	(define y 420)
	(set! x (+ x y))
	(display x)
	(display "\n")
	(display y)
	(display "\n")))

(foo)

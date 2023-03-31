(define fib
  (lambda (n)
	(if (< n 2)
		n
		(+ (fib (- n 1))
		   (fib (- n 2))))))
(display "The 20th fibonacci number is:")
(display (fib 20))

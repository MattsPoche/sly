(define newline
  (lambda () (display "\n")))

(define fib
  (lambda (n)
	(if (< n 2)
		n
		(+ (fib (- n 1))
		   (fib (- n 2))))))

(define fac
  (lambda (n)
	(if (= n 0)
		1
		(* n (fac (- n 1))))))

(display "The 20th \"fibonacci\" number is: ")
(display (fib 20))(newline)
(display "5! = ")
(display (fac 5))(newline)

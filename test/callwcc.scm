(define cc #f)

(define test
  (lambda ()
	(define i 0)
	(call/cc (lambda (k) (set! cc k)))
	(set! i (+ i 1))
	(display i)(display "\n")))

(test)
(cc #f)
(cc #f)
(define other-cc cc)
(test)
(cc #f)
(other-cc #f)

(define newline (lambda () (display "\n")))

(define make-iter
  (lambda ()
	(define i -1)
	(lambda ()
	  (set! i (+ i 1))
	  i)))
(define iter1 (make-iter))
(define iter2 (make-iter))
(display "iter1 -> ")(display (iter1))(newline) ;; 0
(display "iter1 -> ")(display (iter1))(newline) ;; 1
(display "iter1 -> ")(display (iter1))(newline) ;; 2
(display "iter2 -> ")(display (iter2))(newline) ;; 0
(display "iter2 -> ")(display (iter2))(newline) ;; 1
(display "iter2 -> ")(display (iter2))(newline) ;; 2
(display "iter2 -> ")(display (iter2))(newline) ;; 3
(display "iter2 -> ")(display (iter2))(newline) ;; 4
(display "iter1 -> ")(display (iter1))(newline) ;; 3
(display "iter1 -> ")(display (iter1))(newline) ;; 4
(display "iter2 -> ")(display (iter2))(newline) ;; 5

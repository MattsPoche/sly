(define n 0)
((lambda (x y)
   (set! n (+ x y))) 32 55)
(display n)
(display "\n")
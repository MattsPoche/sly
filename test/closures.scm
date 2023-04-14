(define (nl) (display "\n"))

(define (make-pair fst snd)
  (lambda (f)
	(f fst snd)))

(define (fst x y) x)

(define (snd x y) y)

(define p1 (make-pair 69 420))
(define p2 (make-pair "hello" "world"))
(display p1)(nl)
(display (p1 fst))(nl)
(display (p1 snd))(nl)
(display p2)(nl)
(display (p2 fst))(nl)
(display (p2 snd))(nl)

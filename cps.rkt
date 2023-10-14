#lang racket

(require racket/struct)
(require racket/match)

;; core forms:
;; (let ([id val] ...) expr ...)    ; local binding
;; (letrec ([id val] ...) expr ...) ; local recursive binding
;; (lambda (id ...) expr ...)       ; function
;; (set! id val)
;; (f args ...)                     ; function application


(define filename
  (command-line
   #:args (filename) ; expect one command-line argument: <filename>
   ; return the argument as a filename
   filename))

(define (list-no-brackets ls)
  (let ([out (open-output-string)])
    (for-each (lambda (x) (display (format "~a " x) out)) ls)
    (get-output-string out)))

(struct cps-form ())
(struct cps/formal (id value)
  #:super struct:cps-form
  #:mutable
  #:methods gen:custom-write
  [(define write-proc
     (lambda (obj port mode)
       (write-string
        (format "((~a ~a))"
                (cps/formal-id obj)
                (cps/formal-value obj)) port)))])

(struct cps/let (formal expr)
  #:super struct:cps-form
  #:mutable
  #:methods gen:custom-write
  [(define write-proc
     (lambda (obj port mode)
       (write-string
        (format "(let ~a\n~a)"
                (cps/let-formal obj)
                (cps/let-expr obj)) port)))])

(struct cps/letrec (formals expr)
  #:super struct:cps-form
  #:mutable
  #:methods gen:custom-write
  [(define write-proc
     (lambda (obj port mode)
       (write-string
        (format "(letrec ~a ~a)"
                (cps/letrec-formals obj)
                (cps/letrec-expr obj)) port)))])

(struct cps/lambda (args expr)
  #:super struct:cps-form
  #:mutable
  #:methods gen:custom-write
  [(define write-proc
     (lambda (obj port mode)
       (write-string
        (format "(lambda ~a\n~a)"
                (cps/lambda-args obj)
                (cps/lambda-expr obj)) port)))])

(struct cps/call (proc cont args)
  #:super struct:cps-form
  #:mutable
  #:methods gen:custom-write
  [(define write-proc
     (lambda (obj port mode)
       (display `(call ,(cps/call-proc obj)
                       ,(cps/call-cont obj)
                       ,@(cps/call-args obj))
                port)))])

(struct cps/kcall (cont args)
  #:super struct:cps-form
  #:mutable
  #:methods gen:custom-write
  [(define write-proc
     (lambda (obj port mode)
       (display `(kcall ,(cps/kcall-cont obj)
                        ,@(cps/kcall-args obj))
                port)))])

(struct cps/const (value)
  #:super struct:cps-form
  #:mutable
  #:methods gen:custom-write
  [(define write-proc
     (lambda (obj port mode)
       (write-string
        (format "(const ~s)"
                (cps/const-value obj)) port)))])

(define input-file
  (let ([f (open-input-file filename #:mode 'text)])
    (port-count-lines! f)
    f))

(define src (read-syntax filename input-file))
(close-input-port input-file)
(define expr (syntax->datum src))

(define (M expr)
  (match expr
    [(? symbol?) expr]))

(define (make-let-binding e k b v)
  (cps/let (cps/formal k (cps/lambda `(,b) v)) e))

(define (T expr k)
  (match expr
    [`(lambda ,args ,e)
     (let ([$k (gensym '$k)])
       (cps/kcall k (list (cps/lambda (cons $k args) (T e $k)))))]
    [`(begin ,e) (T e k)]
    [`(begin ,e0 ,e1 ...)
     (let ([es (map (lambda (x)
                      (let ([$k (gensym '$k)])
                        (list (T x $k) $k (gensym '$a)))) (cons e0 e1))])
       (foldr (lambda (e es)
                (make-let-binding
                 (car e) (cadr e) (caddr e) es)) (cps/kcall k '()) es)
       )]
    [`(,f)
     (let* ([$k (gensym '$k)]
            [$f (gensym '$f)]
            [$e (T f $k)])
       (make-let-binding $e $k $f (cps/call $f k '())))]
    [`(,f ,e ...)
     (let* ([$f (gensym '$f)]
            [$k (gensym '$k)]
            [f (T f $k)]
            [e (map (lambda (x)
                      (let ([$k (gensym '$k)])
                        (list (T x $k) $k (gensym '$a)))) e)]
            [call (cps/call $f k (map (lambda (x) (list-ref x 2)) e))])
       (make-let-binding f $k $f
                         (foldr (lambda (e es)
                                  (make-let-binding
                                   (car e) (cadr e) (caddr e) es)) call e)))]
    [(? symbol?) (cps/kcall k (list expr))]
    [else (cps/kcall k (list (cps/const expr)))]))

(define (cps-transform expr)
  (cps/lambda '(cps/halt) (T expr 'cps/halt)))

(display "src: ")
(writeln (syntax->datum src))
(cps-transform expr)

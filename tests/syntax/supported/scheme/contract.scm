;; Comprehensive Scheme fixture exercising the highlight query.
#| A block comment spanning
   several lines. |#

(define version "1.2.3")
(define limits '(0 . 100))
(define primes '(2 3 5 7 11))
(define grid #(1 2 3 4))

(define (factorial n)
  (if (<= n 1)
      1
      (* n (factorial (- n 1)))))

(define (fib n)
  (let loop ((a 0) (b 1) (k n))
    (if (= k 0)
        a
        (loop b (+ a b) (- k 1)))))

(define (classify n)
  (cond
    ((< n 0) 'negative)
    ((= n 0) 'zero)
    (else 'positive)))

(define (describe shape)
  (case (car shape)
    ((circle) (string-append "r=" (number->string (cadr shape))))
    ((square) "a square")
    (else "unknown")))

(define counter
  (let ((count 0))
    (lambda ()
      (set! count (+ count 1))
      count)))

(define (process items)
  (map (lambda (x) (* x x))
       (filter even? items)))

(define chars (list #\a #\b #\newline))
(define flags #t)
(define template `(result ,version ,@primes))

#;(this datum is commented out)

(for-each
  (lambda (p) (display p) (newline))
  (process primes))

; Constants and helpers
(define pi 3.14159)

(define (square x)
  (* x x))

(define (greet name)
  (display (string-append "Hello, " name))
  (newline))

(define (classify n)
  (cond
    ((< n 0) 'negative)
    ((= n 0) 'zero)
    (else 'positive)))

(let loop ((i 0) (acc '()))
  (if (>= i 10)
      (reverse acc)
      (loop (+ i 1) (cons i acc))))

(define flag #t)
(define nl #\newline)

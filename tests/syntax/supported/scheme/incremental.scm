(define (handle request)
  (let ((method (car request))
        (path (cdr request)))
    (cond
      ((eq? method 'get) (list 200 path))
      ((eq? method 'post) (list 201 ""))
      (else (list 405 "")))))

(define routes
  (list (cons "/health" handle)
        (cons "/status" handle)))

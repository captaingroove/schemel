(begin
(define compose (lambda (f g) (lambda (x) (f (g x)))))
(define repeat (lambda (f) (compose f f)))
(define combine (lambda (f)
  (lambda (x y)
  (if (null? x) (quote ())
      (f (list (car x) (car y))
         ((combine f) (cdr x) (cdr y)))))))
(define riff-shuffle (lambda (deck) (begin
  (define take (lambda (n seq) (if (<= n 0) (quote ()) (cons (car seq) (take (- n 1) (cdr seq))))))
  (define drop (lambda (n seq) (if (<= n 0) seq (drop (- n 1) (cdr seq)))))
  (define mid (lambda (seq) (/ (length seq) 2)))
  ((combine append) (take (mid deck) deck) (drop (mid deck) deck)))))
(display (list
(riff-shuffle (list 1 2 3 4 5 6 7 8))
((repeat riff-shuffle) (list 1 2 3 4 5 6 7 8))
(riff-shuffle (riff-shuffle (riff-shuffle (list 1 2 3 4 5 6 7 8))))
)))

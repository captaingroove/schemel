(begin
  (define compose (lambda (f g) (lambda (x) (f (g x)))))
  (define repeat (lambda (f) (compose f f)))
  (define twice (lambda (x) (* 2 x)))
  (display ((repeat twice) 5))
)

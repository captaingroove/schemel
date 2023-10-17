(begin
  (define abs (lambda (n) ((if (> n 0) + -) 0 n)))
  (display
    (list (abs -3) (abs 0) (abs 3)))
)

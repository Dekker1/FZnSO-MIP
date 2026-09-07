% `c = a / b`, which is `a = b * c` — quadratic unless `b` is fixed.

include "fzn_float_times.mzn";

predicate fzn_float_div(var float: a, var float: b, var float: c) =
  fzn_float_times(b, c, a);

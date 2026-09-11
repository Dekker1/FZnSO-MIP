% `c = a / b`, which is `a = b * c` — quadratic unless `b` is fixed.

include "fzn_float_times.mzn";
% The fixed-operand overloads of `fzn_float_times`, which live beside the other
% one-row forms rather than in the override point.
include "mip_linear.mzn";

predicate fzn_float_div(var float: a, var float: b, var float: c) =
  fzn_float_times(b, c, a);

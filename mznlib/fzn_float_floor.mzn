% `b = floor(a)`, which is `b <= a < b + 1`.

include "mip_linear.mzn";
include "fzn_float_lin_lt.mzn";

predicate fzn_float_floor(var float: a, var int: b) =
  fzn_float_lin_le([1.0, -1.0], [int2float(b), a], 0.0) /\
    fzn_float_lin_lt([1.0, -1.0], [a, int2float(b)], 1.0);

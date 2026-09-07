% `b - 1 < a <= b`, as `std/redefinitions-2.7.1.mzn` defines it. `int2float(b)`
% costs no column: the backend holds an integer and its float view as one.

include "mip_linear.mzn";
include "fzn_float_lin_lt.mzn";

predicate fzn_float_ceil(var float: a, var int: b) =
  fzn_float_lin_le([1.0, -1.0], [a, int2float(b)], 0.0) /\
    fzn_float_lin_lt([1.0, -1.0], [int2float(b), a], 1.0);

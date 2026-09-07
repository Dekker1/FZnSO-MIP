% A MIP has no strict inequality, so the bound is shifted by
% `MIP_float_epsilon`. This does **not** preserve equivalence — a solution within
% that of the bound is lost — and is why float results compare approximately.

include "mip_linear.mzn";

predicate fzn_float_lin_lt(list of float: coeffs, list of var float: xs, float: bound) =
  fzn_float_lin_le(coeffs, xs, bound - MIP_float_epsilon);

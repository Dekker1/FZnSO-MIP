% `b <-> sum(coeffs .* xs) < bound` over floats.

include "mip_linear.mzn";

predicate fzn_float_lin_lt_reif(
  list of float: coeffs,
  list of var float: xs,
  float: bound,
  var bool: b,
) =
  fzn_float_lin_lt_imp(coeffs, xs, bound, b) /\
    fzn_float_lin_le_imp_not([-c | c in coeffs], xs, -bound, b);

% `b <-> sum(coeffs .* xs) <= bound` over floats.
%
% There is no next float to shift the bound by, so the converse is the strict
% inequality, and strictness costs an epsilon: see `MIP_float_epsilon`.

include "mip_linear.mzn";

predicate fzn_float_lin_le_reif(
  list of float: coeffs,
  list of var float: xs,
  float: bound,
  var bool: b,
) =
  fzn_float_lin_le_imp(coeffs, xs, bound, b) /\
    fzn_float_lin_lt_imp_not([-c | c in coeffs], xs, -bound, b);

% `sum(coeffs .* xs) != total` over floats.
%
% One fresh binary saying which side of `total` the sum falls on, and two big-M
% rows. Both are strict, so both carry the `MIP_float_epsilon` shift.

include "mip_linear.mzn";

predicate fzn_float_lin_ne(list of float: coeffs, list of var float: xs, float: total) =
  let {
    var bool: below;
  } in fzn_float_lin_lt_imp(coeffs, xs, total, below) /\
    fzn_float_lin_lt_imp_not([-c | c in coeffs], xs, -total, below);

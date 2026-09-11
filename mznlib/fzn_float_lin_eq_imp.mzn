% The registry constraint `float_lin_eq_imp`, as one big-M row.
%
% Its own file so that a solver layer can replace just this one: a backend
% with indicator constraints ships a declaration with no body, and the row
% below is never built. See `../README.md`.

include "mip_linear.mzn";

/** @group mip `b -> sum(coeffs .* xs) = total` over floats. */
predicate fzn_float_lin_eq_imp(
  list of float: coeffs,
  list of var float: xs,
  float: total,
  var bool: b,
) =
  if is_fixed(b) then
    if fix(b) then fzn_float_lin_eq(coeffs, xs, total) else true endif
  else
    fzn_float_lin_le_imp(coeffs, xs, total, b) /\
      fzn_float_lin_le_imp([-c | c in coeffs], xs, -total, b)
  endif;

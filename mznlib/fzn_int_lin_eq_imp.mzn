% The registry constraint `int_lin_eq_imp`, as one big-M row.
%
% Its own file so that a solver layer can replace just this one: a backend
% with indicator constraints ships a declaration with no body, and the row
% below is never built. See `../README.md`.

include "mip_linear.mzn";

/** @group mip `b -> sum(coeffs .* xs) = total`, as the two inequalities.
 *
 * A decided indicator first: an equality the backend takes in one row would
 * otherwise reach it as the two inequalities its `_imp` form is made of.
 */
predicate fzn_int_lin_eq_imp(
  list of int: coeffs,
  list of var int: xs,
  int: total,
  var bool: b,
) =
  if is_fixed(b) then
    if fix(b) then fzn_int_lin_eq(coeffs, xs, total) else true endif
  else
    fzn_int_lin_le_imp(coeffs, xs, total, b) /\
      fzn_int_lin_le_imp([-c | c in coeffs], xs, -total, b)
  endif;

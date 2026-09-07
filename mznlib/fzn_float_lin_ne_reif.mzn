% `b <-> sum(coeffs .* xs) != total` over floats. See `fzn_int_lin_ne_reif.mzn`:
% at least one of the two inequalities always holds, so they sum to 2 exactly
% when the equality does. Stating it this way names `b` rather than its negation.

include "mip_linear.mzn";
include "fzn_float_lin_le_reif.mzn";

predicate fzn_float_lin_ne_reif(
  list of float: coeffs,
  list of var float: xs,
  float: total,
  var bool: b,
) =
  if is_fixed(b) then
    if fix(b) then
      fzn_float_lin_ne(coeffs, xs, total)
    else
      fzn_float_lin_eq(coeffs, xs, total)
    endif
  else
    let {
      var bool: le;
      var bool: ge;
      constraint fzn_float_lin_le_reif(coeffs, xs, total, le);
      constraint fzn_float_lin_le_reif([-c | c in coeffs], xs, -total, ge);
    } in fzn_bool_lin_eq([1, 1, 1], [le, ge, b], 2)
  endif;

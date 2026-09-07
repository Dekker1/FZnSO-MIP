% `b <-> sum(coeffs .* xs) = total` over floats.
%
% `1 + b = le + ge`, as for the integer form.

include "mip_linear.mzn";
include "fzn_float_lin_le_reif.mzn";
include "fzn_float_lin_ne.mzn";

predicate fzn_float_lin_eq_reif(
  list of float: coeffs,
  list of var float: xs,
  float: total,
  var bool: b,
) =
  if is_fixed(b) then
    if fix(b) then
      fzn_float_lin_eq(coeffs, xs, total)
    else
      fzn_float_lin_ne(coeffs, xs, total)
    endif
  else
    let {
      var bool: le;
      var bool: ge;
      constraint fzn_float_lin_le_reif(coeffs, xs, total, le);
      constraint fzn_float_lin_le_reif([-c | c in coeffs], xs, -total, ge);
    } in fzn_bool_lin_eq([1, 1, -1], [le, ge, b], 1)
  endif;

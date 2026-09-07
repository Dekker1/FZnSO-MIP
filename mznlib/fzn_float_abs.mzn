% `b = |a|`, as for integers. The sign indicator is left free: the four bounds
% and the two implications already pin `b` to the larger of `a` and `-a`.

include "mip_linear.mzn";

predicate fzn_float_abs(var float: a, var float: b) =
  if lb(a) >= 0.0 then
    fzn_float_lin_eq([1.0, -1.0], [b, a], 0.0)
  elseif ub(a) <= 0.0 then
    fzn_float_lin_eq([1.0, 1.0], [b, a], 0.0)
  else
    let {
      var bool: nonneg;
    } in fzn_float_lin_le([-1.0, 1.0], [b, a], 0.0) /\
      fzn_float_lin_le([-1.0, -1.0], [b, a], 0.0) /\
      b >= 0.0 /\
      b <= max([ub(a), -lb(a)]) /\
      fzn_float_lin_le_imp([1.0, -1.0], [b, a], 0.0, nonneg) /\
      fzn_float_lin_le_imp_not([1.0, 1.0], [b, a], 0.0, nonneg)
  endif;

% `b = |a|`: three lower bounds, and a fresh binary picking which is tight. The
% sign constraint on that binary is redundant, but without it the relaxation is
% merely correct rather than tight.

include "mip_linear.mzn";
include "fzn_int_lin_le_reif.mzn";

predicate fzn_int_abs(var int: a, var int: b) =
  if lb(a) >= 0 then
    fzn_int_lin_eq([1, -1], [b, a], 0)
  elseif ub(a) <= 0 then
    fzn_int_lin_eq([1, 1], [b, a], 0)
  else
    let {
      var bool: nonneg;
    } in fzn_int_lin_le([-1, 1], [b, a], 0) /\
      fzn_int_lin_le([-1, -1], [b, a], 0) /\
      b >= 0 /\
      b <= max([ub(a), -lb(a)]) /\
      fzn_int_lin_le_imp([1, -1], [b, a], 0, nonneg) /\
      fzn_int_lin_le_imp_not([1, 1], [b, a], 0, nonneg) /\
      fzn_int_lin_le_reif([-1], [a], 0, nonneg)
  endif;

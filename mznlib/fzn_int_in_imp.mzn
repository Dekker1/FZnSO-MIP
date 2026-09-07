% `b -> x in values`. Only the parameter-set form is here: for a variable set
% the membership is a Boolean of the set's own bit array, and implying it is no
% cheaper than the equivalence.

include "fznso_set_to_bools.mzn";
include "mip_domains.mzn";
include "mip_linear.mzn";
include "fzn_int_in.mzn";

predicate fzn_int_in_imp(var int: x, set of int: values, var bool: b) =
  if is_fixed(b) then
    if fix(b) then fzn_int_in(x, values) else true endif
  else
    let {
      set of int: si = if has_bounds(x) then values intersect dom(x) else values endif;
    } in if card(si) = 0 then
      not b
    elseif has_bounds(x) /\ dom(x) subset si then
      true
    elseif si = min(si)..max(si) then
      fzn_int_lin_le_imp([-1], [x], -min(si), b) /\
        fzn_int_lin_le_imp([1], [x], max(si), b)
    else
      mip_set_in_encoded(x, si, b, false)
    endif
  endif;

predicate fzn_int_in_imp(int: x, var set of int: values, var bool: b) =
  if x in ub(values) then b -> set2bools(values)[x] else not b endif;

predicate fzn_int_in_imp(var int: x, var set of int: values, var bool: b) =
  b -> set2bools(values)[x];

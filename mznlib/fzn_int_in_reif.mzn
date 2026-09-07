% `int_in_reif` for a MIP. See `fzn_int_in.mzn`: the parameter-set overload is
% the one that pays, and the `var set of int` body is `fznso_nosets/`'s, kept
% here because this file shadows it.

include "fznso_set_to_bools.mzn";
include "mip_domains.mzn";
include "mip_linear.mzn";
include "fzn_int_in.mzn";
include "fzn_int_in_imp.mzn";

predicate fzn_int_in_reif(var int: x, set of int: values, var bool: b) :: promise_total =
  if is_fixed(b) then
    if fix(b) then fzn_int_in(x, values) else mip_int_not_in(x, values) endif
  else
    let {
      set of int: si = if has_bounds(x) then values intersect dom(x) else values endif;
    } in if card(si) = 0 then
      not b
    elseif has_bounds(x) /\ dom(x) subset si then
      b
    elseif mip_domains_wanted(x) then
      set_in_reif__POST(x, si, bool2int(b))
    elseif si = min(si)..max(si) then
      let {
        var bool: above;
        var bool: below;
        constraint fzn_int_lin_le_reif([-1], [x], -min(si), above);
        constraint fzn_int_lin_le_reif([1], [x], max(si), below);
      } in fzn_bool_lin_eq([1, 1, -1], [above, below, b], 1)
    else
      mip_set_in_encoded(x, si, b, true)
    endif
  endif;

predicate fzn_int_in_reif(int: x, var set of int: values, var bool: b) :: promise_total =
  if x in ub(values) then b <-> set2bools(values)[x] else not b endif;

predicate fzn_int_in_reif(var int: x, var set of int: values, var bool: b) :: promise_total =
  b <-> set2bools(values)[x];

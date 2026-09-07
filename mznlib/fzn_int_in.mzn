% `int_in` for a MIP.
%
% A *parameter* set is the case worth separating: the bounds are always worth
% stating, and what is left over is the holes, which the equality encoding says
% in one row against indicators the variable's other constraints share. The
% `var set of int` form below cannot look at the members at all, so it spends an
% element lookup over one Boolean per value of the whole domain.
%
% Both overloads live here because this file shadows `fznso_nosets/`'s, which a
% model with set variables still needs; the second is that file's body.

include "fznso_set_to_bools.mzn";
include "mip_domains.mzn";
include "mip_linear.mzn";

predicate fzn_int_in(var int: x, set of int: values) :: promise_total =
  let {
    set of int: si = if has_bounds(x) then values intersect dom(x) else values endif;
  } in if card(si) = 0 then
    false
  elseif has_bounds(x) /\ dom(x) subset si then
    true
  else
    x >= min(si) /\
      x <= max(si) /\
      (
        if si = min(si)..max(si) then
          true
        elseif mip_domains_wanted(x) then
          set_in__POST(x, si)
        else
          mip_set_in_encoded(x, si, true, true)
        endif
      )
  endif;

predicate fzn_int_in(int: x, var set of int: values) :: promise_total =
  let { array [int] of var bool: by = set2bools(values) } in
    if x in ub(values) then by[x] else false endif;

predicate fzn_int_in(var int: x, var set of int: values) :: promise_total =
  if ub(values) = {} then
    false
  else
    % The bounds are conjuncts rather than `let` constraints: this predicate is
    % `promise_total`, so a constraint in its `let` would be posted at the root
    % even where the membership itself is only implied.
    x >= min(ub(values)) /\
      x <= max(ub(values)) /\
      (
        let {
          set of int: extent = ub(values);
        } in if is_fixed(values) /\ extent = min(extent)..max(extent) then
          true
        else
          let {
            array [int] of var bool: by = set2bools(values);
          } in by[x]
        endif
      )
  endif;

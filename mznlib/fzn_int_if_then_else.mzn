% See `mip_if_then_else.mzn`.

include "mip_if_then_else.mzn";

predicate fzn_int_if_then_else(
  list of var bool: conditions,
  list of var int: values,
  var int: result,
) =
  if mip_is_binary_choice(conditions) then
    mip_int_choice(
      mip_choice_condition(conditions),
      values[min(index_set(values))],
      values[max(index_set(values))],
      result,
    )
  else
    let {
      array [int] of var bool: chosen = mip_first_true(conditions);
    } in if forall (i in index_set(values)) (is_fixed(values[i])) then
      fzn_bool_lin_eq([fix(values[i]) | i in index_set(values)], chosen, result)
    else
      forall (i in index_set(values)) (
        fzn_int_lin_eq_imp([1, -1], [result, values[i]], 0, chosen[i])
      )
    endif
  endif;

% `result = if c then a else b endif`. Two constant branches are one row through
% `b + (a - b) * c`, which is why the fixed case is worth separating.
predicate mip_int_choice(var bool: c, var int: a, var int: b, var int: result) =
  if is_fixed(a) /\ is_fixed(b) then
    fzn_int_lin_eq([fix(a) - fix(b), -1], [bool2int(c), result], -fix(b))
  else
    fzn_int_lin_eq_imp([1, -1], [result, a], 0, c) /\
      fzn_int_lin_eq_imp_not([1, -1], [result, b], 0, c)
  endif;

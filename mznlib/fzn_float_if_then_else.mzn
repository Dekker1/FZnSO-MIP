% See `mip_if_then_else.mzn`.

include "mip_if_then_else.mzn";

predicate fzn_float_if_then_else(
  list of var bool: conditions,
  list of var float: values,
  var float: result,
) =
  if mip_is_binary_choice(conditions) then
    mip_float_choice(
      mip_choice_condition(conditions),
      values[min(index_set(values))],
      values[max(index_set(values))],
      result,
    )
  else
    let {
      array [int] of var bool: chosen = mip_first_true(conditions);
    } in if forall (i in index_set(values)) (is_fixed(values[i])) then
      fzn_float_lin_eq(
        [fix(values[i]) | i in index_set(values)] ++ [-1.0],
        [int2float(bool2int(chosen[i])) | i in index_set(chosen)] ++ [result],
        0.0,
      )
    else
      forall (i in index_set(values)) (
        fzn_float_lin_eq_imp([1.0, -1.0], [result, values[i]], 0.0, chosen[i])
      )
    endif
  endif;

% See `mip_int_choice`.
predicate mip_float_choice(
  var bool: c,
  var float: a,
  var float: b,
  var float: result,
) =
  if is_fixed(a) /\ is_fixed(b) then
    fzn_float_lin_eq(
      [fix(a) - fix(b), -1.0],
      [int2float(bool2int(c)), result],
      -fix(b),
    )
  else
    fzn_float_lin_eq_imp([1.0, -1.0], [result, a], 0.0, c) /\
      fzn_float_lin_eq_imp_not([1.0, -1.0], [result, b], 0.0, c)
  endif;

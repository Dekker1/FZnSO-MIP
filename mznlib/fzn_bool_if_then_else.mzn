% See `mip_if_then_else.mzn`. The result is a Boolean, so the branch is tied to
% it by the two clauses an equivalence is.

include "mip_if_then_else.mzn";
include "fzn_bool_clause.mzn";

predicate fzn_bool_if_then_else(
  list of var bool: conditions,
  list of var bool: values,
  var bool: result,
) =
  if mip_is_binary_choice(conditions) then
    % The negated branch indicator is the condition moved to the other side of
    % the clause, so neither clause pair needs it as a variable.
    let {
      var bool: c = mip_choice_condition(conditions);
      var bool: a = values[min(index_set(values))];
      var bool: b = values[max(index_set(values))];
    } in fzn_bool_clause([result], [c, a]) /\
      fzn_bool_clause([a], [c, result]) /\
      fzn_bool_clause([result, c], [b]) /\
      fzn_bool_clause([b, c], [result])
  else
    let {
      array [int] of var bool: chosen = mip_first_true(conditions);
    } in forall (i in index_set(values)) (
      fzn_bool_clause([result], [chosen[i], values[i]]) /\
        fzn_bool_clause([values[i]], [chosen[i], result])
    )
  endif;

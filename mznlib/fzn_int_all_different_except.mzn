% All different but for the excepted values, which may repeat.
%
% The same equality encoding as `fzn_int_all_different`, with the excepted
% values' rows left out — including its narrowing: a value only one variable can
% take constrains nothing, and asking for its indicator would build that
% variable's whole encoding for no reason.

include "mip_linear.mzn";
include "fzn_bool_clause.mzn";

predicate fzn_int_all_different_except(list of var int: xs, set of int: except) =
  if length(xs) <= 1 then
    true
  elseif forall (i in index_set(xs)) (has_bounds(xs[i])) then
    forall (d in (lb_array(xs)..ub_array(xs)) diff except) (
      let {
        array [int] of int: takers = [i | i in index_set(xs) where d in dom(xs[i])];
      } in if length(takers) <= 1 then
        true
      else
        fzn_bool_lin_le(
          [1 | i in takers],
          [mip_select_at(xs[i], d) | i in takers],
          1,
        )
      endif
    )
  else
    forall (i, j in index_set(xs) where i < j) (
      fzn_bool_clause([xs[i] in except], [xs[i] = xs[j]]) /\
        fzn_bool_clause([xs[j] in except], [xs[i] = xs[j]])
    )
  endif;

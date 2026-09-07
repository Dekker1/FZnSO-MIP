% One row per value over the selector binaries [Refalo, CP 2000], rather than
% `n * (n-1) / 2` disequalities each costing a fresh binary and two big-M rows.
% Unbounded variables have no encoding to build, so they fall back to those.
%
% Only values two or more variables can take get a row, and only those variables
% appear in it: a value one variable can take constrains nothing, and asking for
% an indicator would build that variable's encoding for no reason.

include "mip_linear.mzn";
include "fzn_int_lin_ne.mzn";

predicate fzn_int_all_different(list of var int: xs) =
  if length(xs) <= 1 then
    true
  elseif forall (i in index_set(xs)) (has_bounds(xs[i])) then
    forall (d in lb_array(xs)..ub_array(xs)) (
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
      fzn_int_lin_ne([1, -1], [xs[i], xs[j]], 0)
    )
  endif;

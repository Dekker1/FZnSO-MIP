% One binary per tuple and one row per column: `n + 1` rows and `m` binaries,
% against the `m * n` reified equalities the standard decomposition costs.

include "mip_linear.mzn";

predicate fzn_int_table(list of var int: xs, list of int: tuples) =
  let {
    int: n = length(xs);
  } in if n = 0 then
    true
  else
    let {
      int: m = length(tuples) div n;
      array [1..m] of var bool: chosen;
      constraint fzn_bool_lin_eq([1 | i in 1..m], chosen, 1);
    } in forall (j in 1..n) (
      % `tuples` is row-major, so row `i`'s column `j` is at `(i - 1) * n + j`.
      fzn_bool_lin_eq([tuples[(i - 1) * n + j] | i in 1..m], chosen, xs[j])
    )
  endif;

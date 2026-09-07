% One row per pair, because the equality encoding already holds the indicator
% for `f[i] = j` and the one for `invf[j] = i` as the same binary. Reifying both
% equalities and linking them, as the standard decomposition does, is four
% big-M rows where this is one.

include "global_cardinality.mzn";
include "mip_linear.mzn";

predicate fzn_int_inverse_in_range(
  list of var int: f,
  int: f_offset,
  list of var int: invf,
  int: invf_offset,
) =
  if forall (i in index_set(f)) (has_bounds(f[i])) /\
    forall (j in index_set(invf)) (has_bounds(invf[j])) then
    forall (i in index_set(f), j in index_set(invf)) (
      fzn_bool_lin_eq(
        [1, -1],
        [
          mip_select_at(f[i], j + invf_offset - 1),
          mip_select_at(invf[j], i + f_offset - 1),
        ],
        0,
      )
    )
  else
    forall (i in index_set(f), j in index_set(invf)) (
      (f[i] = j + invf_offset - 1) = (invf[j] = i + f_offset - 1)
    )
  endif;

% Unused here — the equality encoding already says one variable takes each
% value — but `fzn_int_inverse_in_range_reif.mzn` calls it, and this file
% shadows the `fznso_constraints/` one that used to declare it.
predicate strengthen_injection_for_inverse_in_range(
  array [int] of var int: f,
  array [int] of var int: invf,
) =
  let {
    set of int: sinvf = {i | i in index_set(invf) where dom(invf[i]) subset index_set(f)};
  } in global_cardinality(f, [i | i in sinvf], [1 | i in sinvf]);

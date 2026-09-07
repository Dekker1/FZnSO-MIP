% `fznso_constraints/` has the `_ne` and `_le` reified forms of `bool_lin_*` but
% not this one, and `mip_select` needs it: MiniZinc only reifies a call whose
% reified form exists.
%
% The `bool2int` per element costs nothing — a total function between two
% decisions, which the backend holds in one column.

include "fzn_int_lin_eq_reif.mzn";

predicate fzn_bool_lin_eq_reif(
  list of int: coeffs,
  list of var bool: xs,
  var int: total,
  var bool: b,
) =
  fzn_int_lin_eq_reif(
    coeffs ++ [-1],
    [bool2int(xs[i]) | i in index_set(xs)] ++ [total],
    0,
    b,
  );

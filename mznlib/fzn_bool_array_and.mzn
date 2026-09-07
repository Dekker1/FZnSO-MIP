% `r <-> forall(xs)`. Nothing here converts a Boolean to an integer.
%
% The one-way implications are written as implications rather than as rows: a
% MIP row over two binaries and a clause are the same constraint, but only the
% clause is something MiniZinc's own optimiser can chain-compress. Neither
% `bool_le` nor `bool_clause` rewrites back to this predicate, so there is no
% loop to close.

include "mip_linear.mzn";
include "fzn_bool_array_and_imp.mzn";

predicate fzn_bool_array_and(list of var bool: xs, var bool: r) =
  if length(xs) = 0 then
    r % The empty conjunction holds.
  else
    forall (i in index_set(xs)) (r -> xs[i]) /\
      % `r` is only free to be false when some element is: one row, because a
      % clause over the negations would need every one of them.
      fzn_bool_lin_le([1 | i in index_set(xs)] ++ [-1], xs ++ [r], length(xs) - 1)
  endif;

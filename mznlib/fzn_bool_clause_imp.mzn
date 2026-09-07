% `b -> (or(pos) \/ or(not neg))`, which is that clause with `b` negated into
% it: one row, where reifying the clause and implying the result costs a
% Boolean, a linking clause and the converse nobody asked for.
%
% Every half-reified Boolean form the rewrite layer lowers onto a clause is
% derived from this one, so it is the only `_imp` the Boolean family needs.

include "fzn_bool_clause.mzn";

predicate fzn_bool_clause_imp(
  list of var bool: pos,
  list of var bool: neg,
  var bool: b,
) = fzn_bool_clause(pos, neg ++ [b]);

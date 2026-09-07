% `b <-> (or(pos) \/ or(not neg))`, as the reified linear inequality the clause
% is: `sum(pos) - sum(neg) + |neg| >= 1`. This is `linear/`'s
% `redefinitions-2.0.mzn`, and it is two big-M rows.
%
% The disaggregated form — one clause per literal for `lit -> b` — is a tighter
% relaxation, but it is `length(pos) + length(neg)` rows against these two, and
% a reified clause is common enough for that to dominate a model.

include "mip_linear.mzn";
include "fzn_bool_clause.mzn";
include "fzn_bool_clause_imp.mzn";

predicate fzn_bool_clause_reif(list of var bool: pos, list of var bool: neg, var bool: b) =
  if is_fixed(b) then
    if fix(b) then
      fzn_bool_clause(pos, neg)
    else
      forall (i in index_set(pos)) (not pos[i]) /\ forall (j in index_set(neg)) (neg[j])
    endif
  elseif length(pos) = 0 /\ length(neg) = 0 then
    not b % The empty clause never holds.
  else
    fzn_int_lin_le_reif(
      [-1 | i in index_set(pos)] ++ [1 | j in index_set(neg)],
      [bool2int(pos[i]) | i in index_set(pos)] ++
        [bool2int(neg[j]) | j in index_set(neg)],
      length(neg) - 1,
      b,
    )
  endif;

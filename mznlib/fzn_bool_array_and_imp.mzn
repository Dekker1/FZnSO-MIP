% `b -> forall(xs)`, which is one clause per element: `b -> xs[i]`. The
% equivalence below it needs a row saying `b` may only be false when some
% element is; an implication says nothing about that direction.

include "fzn_bool_clause.mzn";

predicate fzn_bool_array_and_imp(list of var bool: xs, var bool: b) =
  forall (i in index_set(xs)) (fzn_bool_clause([xs[i]], [b]));

% See `mip_lex.mzn`.

include "mip_lex.mzn";

predicate fzn_bool_lex_lesseq(list of var bool: xs, list of var bool: ys) =
  mip_bool_lex(xs, ys, false);

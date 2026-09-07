% See `mip_lex.mzn`.

include "mip_lex.mzn";

predicate fzn_int_lex_lesseq(list of var int: xs, list of var int: ys) =
  mip_int_lex(xs, ys, false);

% See `mip_lex.mzn`.

include "mip_lex.mzn";

predicate fzn_float_lex_lesseq(list of var float: xs, list of var float: ys) =
  mip_float_lex(xs, ys, false);

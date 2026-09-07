% Each column of the row-major array is lexicographically at most the next.
%
% See `mip_lex.mzn`.

include "mip_lex.mzn";

predicate fzn_int_lex_chain_lesseq(list of var int: xs, int: columns) =
  if columns < 2 then
    true
  else
    let {
      int: rows = length(xs) div columns;
    } in forall (j in 1..columns - 1) (
      mip_int_lex(
        [xs[(i - 1) * columns + j] | i in 1..rows],
        [xs[(i - 1) * columns + j + 1] | i in 1..rows],
        false,
      )
    )
  endif;

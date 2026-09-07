% `std/redefinitions-2.7.1.mzn`'s own definition. The shifted value is inline
% rather than named so MiniZinc derives its domain from `a`'s: a named
% intermediate would start unbounded, and a big-M needs a finite bound.

include "fzn_float_ceil.mzn";
include "fzn_float_floor.mzn";

predicate fzn_float_round(var float: a, var int: b) =
  if lb(a) >= 0.0 then
    fzn_float_floor(a + 0.5, b)
  elseif ub(a) <= 0.0 then
    fzn_float_ceil(a - 0.5, b)
  else
    if a < 0.0 then fzn_float_ceil(a - 0.5, b) else fzn_float_floor(a + 0.5, b) endif
  endif;

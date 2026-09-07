% `c = a mod b`, taking the sign of the dividend. See `mip_division.mzn`.

include "mip_division.mzn";

predicate fzn_int_mod(var int: a, var int: b, var int: c) =
  fzn_int_lin_eq([1, -1], [c, mip_int_divmod(a, b)[2]], 0);

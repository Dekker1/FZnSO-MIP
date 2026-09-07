% See `mip_extrema.mzn`, where the shape all four share is written once.

include "mip_extrema.mzn";

predicate fzn_int_array_minimum(list of var int: xs, var int: value) =
  mip_int_extremum(xs, value, false);

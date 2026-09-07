% See `mip_extrema.mzn`, where the shape all four share is written once.

include "mip_extrema.mzn";

predicate fzn_float_array_maximum(list of var float: xs, var float: value) =
  mip_float_extremum(xs, value, true);

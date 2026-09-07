% See `mip_element.mzn`.

include "mip_element.mzn";

predicate fzn_float_array_element(
  list of var float: xs,
  int: offset,
  var int: index,
  var float: value,
) = mip_float_element(xs, offset, index, value);

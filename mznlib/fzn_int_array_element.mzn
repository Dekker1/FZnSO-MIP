% See `mip_element.mzn`.

include "mip_element.mzn";

predicate fzn_int_array_element(
  list of var int: xs,
  int: offset,
  var int: index,
  var int: value,
) = mip_int_element(xs, offset, index, value);

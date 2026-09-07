% See `mip_element.mzn`.

include "mip_element.mzn";

predicate fzn_bool_array_element(
  list of var bool: xs,
  int: offset,
  var int: index,
  var bool: value,
) = mip_bool_element(xs, offset, index, value);

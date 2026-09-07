% The row-major index is one linear row, and what is left is the flat element.
%
% See `mip_element.mzn`.

include "mip_element.mzn";

predicate fzn_float_array_element_nd(
  list of var float: xs,
  list of int: offsets,
  list of int: sizes,
  list of var int: indices,
  var float: value,
) =
  mip_float_element(xs, 1, mip_position_nd(length(xs), offsets, sizes, indices), value);

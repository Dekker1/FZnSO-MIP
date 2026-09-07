% `lower_bound <= x <= upper_bound`, as the two rows.

include "mip_linear.mzn";

predicate fzn_float_in(var float: x, float: lower_bound, float: upper_bound) =
  x <= upper_bound /\ x >= lower_bound;

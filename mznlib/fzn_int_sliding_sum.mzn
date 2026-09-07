% Every window of `window` consecutive elements sums within the bounds.
%
% Two rows per window, straight over the variables: there is no intermediate
% sum variable, which is what the standard decomposition introduces.

include "mip_linear.mzn";

predicate fzn_int_sliding_sum(
  list of var int: xs,
  int: window,
  int: lower_bound,
  int: upper_bound,
) =
  forall (i in 1..length(xs) - window + 1) (
    fzn_int_lin_le([1 | j in 1..window], [xs[i + j - 1] | j in 1..window], upper_bound) /\
      fzn_int_lin_le([-1 | j in 1..window], [xs[i + j - 1] | j in 1..window], -lower_bound)
  );

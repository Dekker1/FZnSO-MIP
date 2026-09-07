% Array minimum and maximum. Bounded on one side by every element that could
% attain it, and a binary per such element says which one does. All four
% registry forms are this shape, so the `fzn_` files beside it are
% instantiations.
%
% Only the elements that *can* attain the extremum get a binary and a big-M row.
% An element whose whole range lies past the tightest opposite bound can never
% be the answer, and its one-sided bound is already implied by the bound on the
% result — so saying anything about it costs a binary and a row for nothing.
% Where only one element is left the extremum is that element, and the whole
% constraint is one row. This is `linear/`'s `array_int_minimum_I`.

include "mip_linear.mzn";

function set of int: mip_extremum_candidates(array [int] of var int: xs, bool: maximum) =
  if exists (i in index_set(xs)) (not has_bounds(xs[i])) then
    index_set(xs)
  elseif maximum then
    let {
      int: tightest = max (i in index_set(xs)) (lb(xs[i]));
      set of int: could = {i | i in index_set(xs) where ub(xs[i]) > tightest};
      set of int: attains = {i | i in index_set(xs) where lb(xs[i]) = tightest};
    } in if card(could intersect attains) > 0 then could else could union {min(attains)} endif
  else
    let {
      int: tightest = min (i in index_set(xs)) (ub(xs[i]));
      set of int: could = {i | i in index_set(xs) where lb(xs[i]) < tightest};
      set of int: attains = {i | i in index_set(xs) where ub(xs[i]) = tightest};
    } in if card(could intersect attains) > 0 then could else could union {min(attains)} endif
  endif;

predicate mip_int_extremum(list of var int: xs, var int: value, bool: maximum) =
  assert(length(xs) > 0, "the minimum of an empty array is not defined") /\
    let {
      set of int: at = mip_extremum_candidates(xs, maximum);
      array [int] of int: candidate = [i | i in at];
    } in if card(at) = 1 then
      fzn_int_lin_eq([1, -1], [value, xs[min(at)]], 0)
    else
      let {
        int: sign = if maximum then -1 else 1 endif;
        array [index_set(candidate)] of var bool: chosen;
      } in
        forall (i in at) (fzn_int_lin_le([sign, -sign], [value, xs[i]], 0)) /\
        fzn_bool_lin_eq(
          [1 | j in index_set(candidate)],
          [chosen[j] | j in index_set(candidate)],
          1,
        ) /\
        forall (j in index_set(candidate)) (
          fzn_int_lin_le_imp([-sign, sign], [value, xs[candidate[j]]], 0, chosen[j])
        )
    endif;

% The constant entries, as the one that stands for all of them. Only the
% extreme constant can be the answer, and the bound it puts on the result
% implies the bound every other constant puts there — so this drops rows
% without dropping anything the rows said.
%
% Floats stop here. `mip_extremum_candidates` would go further and rule out
% *variables* that cannot attain the extremum, but that leaves MiniZinc unable
% to infer bounds for the result of `min_t`, which it derives by looking at
% what the decomposition says about it.
function array [int] of var float: mip_float_extremum_live(
  array [int] of var float: xs,
  bool: maximum,
) =
  let {
    array [int] of var float: free =
      [xs[i] | i in index_set(xs) where not is_fixed(xs[i])];
    array [int] of float: fixed = [fix(xs[i]) | i in index_set(xs) where is_fixed(xs[i])];
  } in if length(fixed) = 0 then
    free
  else
    free ++ [if maximum then max(fixed) else min(fixed) endif]
  endif;

predicate mip_float_extremum(list of var float: xs, var float: value, bool: maximum) =
  assert(length(xs) > 0, "the minimum of an empty array is not defined") /\
    let {
      array [int] of var float: live = mip_float_extremum_live(array1d(xs), maximum);
    } in if length(live) = 1 then
      fzn_float_lin_eq([1.0, -1.0], [value, live[min(index_set(live))]], 0.0)
    else
      let {
        float: sign = if maximum then -1.0 else 1.0 endif;
        array [index_set(live)] of var bool: at;
      } in forall (i in index_set(live)) (
        fzn_float_lin_le([sign, -sign], [value, live[i]], 0.0)
      ) /\
        fzn_bool_lin_eq([1 | i in index_set(live)], [at[i] | i in index_set(live)], 1) /\
        forall (i in index_set(live)) (
          fzn_float_lin_le_imp([-sign, sign], [value, live[i]], 0.0, at[i])
        )
    endif;

% An odd number of `xs` hold. `sum(xs) - 2 * m = 1` for a fresh `m` is the only
% linear way to say "odd". The sum is written over `bool2int` terms so that it
% and the parity are one row: the conversions cost nothing, being the same
% column as the Boolean they convert.
%
% A fixed element is folded out first, because it only flips which parity the
% rest must have — and what is left is usually two Booleans, where one parity is
% an equality the compiler resolves without a row and the other a negation. This
% is where `linear/`'s `bool_eq_reif` gets its `is_fixed` cases from: a reified
% equality between Booleans arrives here as a three-element parity, and with one
% of the three a constant it is an alias.

include "mip_linear.mzn";

predicate fzn_bool_array_xor(list of var bool: xs) =
  let {
    % Fixed elements decide their own contribution, so only the parity of how
    % many of them hold survives.
    set of int: live = {i | i in index_set(xs) where not is_fixed(xs[i])};
    int: held = sum (i in index_set(xs) where is_fixed(xs[i])) (bool2int(fix(xs[i])));
  } in mip_bool_parity([xs[i] | i in live], 1 - held mod 2);

/** @group mip Whether an odd (`want = 1`) or even (`want = 0`) number of `xs`
 * hold, for `xs` that are all free.
 */
predicate mip_bool_parity(array [int] of var bool: xs, int: want) =
  let {
    int: n = length(xs);
  } in if n = 0 then
    % Nothing holds, so the count is zero, which is even.
    want = 0
  elseif n = 1 then
    if want = 1 then xs[min(index_set(xs))] else not xs[min(index_set(xs))] endif
  elseif n = 2 then
    % Two Booleans of odd parity are each other's negation, and of even parity
    % they are the same Boolean — which the compiler resolves by unifying them,
    % so that one costs no row at all.
    fzn_bool_lin_eq(
      [1, if want = 1 then 1 else -1 endif],
      [xs[i] | i in index_set(xs)],
      want,
    )
  else
    let {
      var 0..(n - want) div 2: m;
    } in fzn_int_lin_eq(
      [1 | i in index_set(xs)] ++ [-2],
      [bool2int(xs[i]) | i in index_set(xs)] ++ [m],
      want,
    )
  endif;

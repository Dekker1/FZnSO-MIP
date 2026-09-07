% `1 + b = le + ge` over the two inequalities reified: both hold exactly when
% the sum equals `total`. `linear/`'s `aux_int_eq_iff_1` with the Booleans left
% as Booleans.

include "mip_linear.mzn";
include "fzn_bool_array_xor.mzn";
include "mip_domains.mzn";
include "fzn_int_lin_le_reif.mzn";
include "fzn_int_lin_ne.mzn";

predicate fzn_int_lin_eq_reif(
  list of int: coeffs,
  list of var int: xs,
  int: total,
  var bool: b,
) =
  % A fixed `b` first: it decides the constraint outright, and it must not reach
  % a `__POST` call, whose indicator would then be a literal rather than the
  % variable the pass reads.
  if is_fixed(b) then
    if fix(b) then
      fzn_int_lin_eq(coeffs, xs, total)
    else
      fzn_int_lin_ne(coeffs, xs, total)
    endif
  % `b <-> x = v` once the fixed terms are folded away, which is how
  % `linear/`'s `int_eq_reif(var int, int)` overload arrives here.
  elseif mip_eq_reif_disjoint(coeffs, xs, total) then
    % Two variables that can never be equal: no rows at all.
    not b
  elseif mip_eq_reif_two_valued(coeffs, xs, total) then
    let {
      array [int] of int: free = mip_free_terms(xs);
    } in mip_int_eq_reif_two_valued(xs[free[1]], xs[free[2]], b)
  elseif mip_eq_reif_is_single(coeffs, xs) then
    mip_int_eq_reif_single(coeffs, xs, total - mip_fixed_sum(coeffs, xs), b)
  else
    fzn_int_lin_eq_reif__general(coeffs, xs, total, b)
  endif;

% Exactly one free term, with a unit coefficient and a finite domain.
test mip_eq_reif_is_single(array [int] of int: coeffs, array [int] of var int: xs) =
  let { array [int] of int: free = mip_free_terms(xs) } in
    length(free) = 1 /\ (coeffs[free[1]] = 1 \/ coeffs[free[1]] = -1) /\
      has_bounds(xs[free[1]]);

% `b <-> coeffs[j] * x = rest`, for the one free term `j`.
predicate mip_int_eq_reif_single(
  array [int] of int: coeffs,
  array [int] of var int: xs,
  int: rest,
  var bool: b,
) =
  let {
    array [int] of int: free = mip_free_terms(xs);
    var int: x = xs[free[1]];
    int: v = coeffs[free[1]] * rest;
  } in if not (v in dom(x)) then
    not b
  elseif card(dom(x)) = 2 then
    if v = ub(x) then mip_two_valued(x, b) else mip_two_valued_not(x, b) endif
  elseif mip_domains_encodes(x) then
    % The pass is going to encode this variable anyway, so the encoding below
    % would be the second one. Hand the comparison over and it reads the first.
    mip_int_eq_reif_posted(x, v, b)
  elseif card(dom(x)) <= MIP_max_equality_encoding then
    % One row against the encoding's own indicator. Every value of one variable
    % shares the encoding, so this beats four big-M rows as soon as a second
    % value of `x` is reified.
    fzn_bool_lin_eq([1, -1], [mip_select_at(x, v), b], 0)
  else
    fzn_int_lin_eq_reif__general([1], [x], v, b)
  endif;

predicate fzn_int_lin_eq_reif__general(
  list of int: coeffs,
  list of var int: xs,
  int: total,
  var bool: b,
) =
  let {
    var bool: le;
    var bool: ge;
    constraint fzn_int_lin_le_reif(coeffs, xs, total, le);
    constraint fzn_int_lin_le_reif([-c | c in coeffs], xs, -total, ge);
  } in fzn_bool_lin_eq([1, 1, -1], [le, ge, b], 1);

/** @group mip Whether the equality is between two variables that cannot meet.
 *
 * `linear/`'s var/var overload tests `card(dom(x) intersect dom(y)) > 0` before
 * anything else. The single-term gate above cannot see this shape, and a
 * decomposition that reifies an equality for every *pair* of something — the
 * `cumulatives` decomposition compares every pair of tasks' machines — pays the
 * general form's five rows on every pair the domains already rule out.
 *
 * The bound checks inside the big-M rows would eventually fix `b` too, but only
 * after the rows exist and the optimiser folds them away.
 */
test mip_eq_reif_disjoint(array [int] of int: coeffs, array [int] of var int: xs, int: total) =
  let { array [int] of int: free = mip_free_terms(xs) } in
    length(free) = 2 /\
      coeffs[free[1]] = -coeffs[free[2]] /\
      abs(coeffs[free[1]]) = 1 /\
      has_bounds(xs[free[1]]) /\
      has_bounds(xs[free[2]]) /\
      % Cheap guard: the shifted-domain set below is built value by value.
      card(dom(xs[free[2]])) <= MIP_max_equality_encoding /\
      (
        let {
          % `c * x - c * y = rest` with `c = +/-1`, so `x = y + c * rest`.
          int: shift = coeffs[free[1]] * (total - mip_fixed_sum(coeffs, xs));
        } in card(
          dom(xs[free[1]]) intersect {v + shift | v in dom(xs[free[2]])},
        ) = 0
      );

/** @group mip Whether the equality is `x = y` between two variables that take
 * the same two values.
 *
 * The common shape wherever a decomposition compares two members of the same
 * small enumerated set — `cumulatives` asks whether two tasks are on the same
 * machine, for every pair of tasks.
 */
test mip_eq_reif_two_valued(array [int] of int: coeffs, array [int] of var int: xs, int: total) =
  let { array [int] of int: free = mip_free_terms(xs) } in
    length(free) = 2 /\
      coeffs[free[1]] = -coeffs[free[2]] /\
      abs(coeffs[free[1]]) = 1 /\
      total = mip_fixed_sum(coeffs, xs) /\
      has_bounds(xs[free[1]]) /\
      has_bounds(xs[free[2]]) /\
      card(dom(xs[free[1]])) = 2 /\
      dom(xs[free[1]]) = dom(xs[free[2]]);

/** @group mip `b <-> x = y`, for two variables over the same two values.
 *
 * Each variable is its own indicator, so the values agree exactly when the
 * indicators do — and "two Booleans agree" is the parity of the two of them
 * together with `b`, which is one row. The general form below spends five: two
 * reified inequalities and a row tying them to `b`.
 */
predicate mip_int_eq_reif_two_valued(var int: x, var int: y, var bool: b) =
  fzn_bool_array_xor([mip_two_valued_at(x), mip_two_valued_at(y), b]);

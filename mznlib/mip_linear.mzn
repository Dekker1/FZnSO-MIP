% Half reification, which everything else in this library is built from.
% `fzn_int_lin_le_imp` is the only predicate that knows how a backend does it —
% one big-M row here, an indicator constraint for a backend that has them.
%
% Every body names an `fzn_` predicate explicitly. Writing `sum(...) <= k` would
% flatten back to `int_lin_le`, which the rewrite layer sends here again, and a
% rewrite loop hangs the flattener with no message at all.

include "options.mzn";
include "fzn_int_lin_eq.mzn";
include "fzn_int_lin_le.mzn";
include "fzn_float_lin_eq.mzn";
include "fzn_float_lin_le.mzn";
include "fzn_bool_lin_eq.mzn";
include "fzn_bool_lin_le.mzn";
include "fzn_bool_to_int.mzn";
include "mip_domains.mzn";
include "fzn_int_lin_le_imp.mzn";
include "fzn_int_lin_eq_imp.mzn";
include "fzn_float_lin_le_imp.mzn";
include "fzn_float_lin_eq_imp.mzn";
% Nothing here calls it, but `mip_select` states itself with `fzn_bool_lin_eq`
% and MiniZinc only reifies a call whose reified form is declared.
include "fzn_bool_lin_eq_reif.mzn";

% The big-M coefficient is the slack between the bound and the most the
% left-hand side can reach. A zero coefficient contributes nothing, so an
% unbounded variable under one is not an obstacle.

function int: mip_int_lin_ub(array [int] of int: coeffs, array [int] of var int: xs) =
  if forall (i in index_set(xs)) (coeffs[i] = 0 \/ has_bounds(xs[i])) then
    sum (i in index_set(xs)) (
      if coeffs[i] = 0 then
        0
      elseif coeffs[i] > 0 then
        coeffs[i] * ub(xs[i])
      else
        coeffs[i] * lb(xs[i])
      endif
    )
  else
    infinity
  endif;

function int: mip_int_lin_lb(array [int] of int: coeffs, array [int] of var int: xs) =
  let {
    int: hi = mip_int_lin_ub([-c | c in coeffs], xs);
  } in if hi = infinity then -infinity else -hi endif;

function float: mip_float_lin_ub(array [int] of float: coeffs, array [int] of var float: xs) =
  if forall (i in index_set(xs)) (coeffs[i] = 0.0 \/ has_bounds(xs[i])) then
    sum (i in index_set(xs)) (
      if coeffs[i] = 0.0 then
        0.0
      elseif coeffs[i] > 0.0 then
        coeffs[i] * ub(xs[i])
      else
        coeffs[i] * lb(xs[i])
      endif
    )
  else
    infinity
  endif;

function float: mip_float_lin_lb(array [int] of float: coeffs, array [int] of var float: xs) =
  let {
    float: hi = mip_float_lin_ub([-c | c in coeffs], xs);
  } in if hi = infinity then -infinity else -hi endif;

% `c = a * b` with one operand a value, which makes it linear. These are
% overloads of `fzn_float_times`, not branches inside it: the all-variable form
% is the solver's — `fznso_constraints/fzn_float_times.mzn` declares it, and a
% backend declaring `float_times` replaces that declaration.
%
% Written as the equation rather than a hand-built `fzn_float_lin_eq`, which is
% the one exception to this file's rule: the rule exists because the natural
% form of a *linear* body flattens back to the predicate stating it, and the
% natural form of this one is `float_lin_eq`, a different constraint. Stating it
% leaves the flattener free to fold the product into a surrounding linear
% expression, share it through CSE and propagate its bounds.

predicate fzn_float_times(float: a, var float: b, var float: c) = c = a * b;

predicate fzn_float_times(var float: a, float: b, var float: c) = c = a * b;

/** @group mip `not b -> sum(coeffs .* xs) <= bound`, as one big-M row.
 *
 * The converse of a reified comparison needs the *negated* indicator, and
 * writing `not b` there would materialise a second Boolean and a row tying it
 * to the first — one of each per reification. The big-M coefficient carries the
 * polarity instead, so the row is stated against `b` itself.
 */
predicate fzn_int_lin_le_imp_not(
  list of int: coeffs,
  list of var int: xs,
  int: bound,
  var bool: b,
) =
  let {
    int: hi = mip_int_lin_ub(coeffs, xs);
  } in if is_fixed(b) then
    if fix(b) then true else fzn_int_lin_le(coeffs, xs, bound) endif
  elseif hi <= bound then
    true
  elseif mip_int_lin_lb(coeffs, xs) > bound then
    b % It can never fail.
  else
    assert(
      hi < infinity,
      "This model needs a finite upper bound on `\(coeffs) . \(xs)` for a big-M " ++
        "constraint, but at least one variable in it is unbounded. Give the " ++
        "variables in the model finite domains.",
    ) /\
      % At `b = 0` this is the row itself; at `b = 1` the bound implies it.
      fzn_int_lin_le(coeffs ++ [-(hi - bound)], xs ++ [bool2int(b)], bound)
  endif;

/** @group mip `not b -> sum(coeffs .* xs) = total`, as the two inequalities. */
predicate fzn_int_lin_eq_imp_not(
  list of int: coeffs,
  list of var int: xs,
  int: total,
  var bool: b,
) =
  if is_fixed(b) then
    if fix(b) then true else fzn_int_lin_eq(coeffs, xs, total) endif
  else
    fzn_int_lin_le_imp_not(coeffs, xs, total, b) /\
      fzn_int_lin_le_imp_not([-c | c in coeffs], xs, -total, b)
  endif;

/** @group mip `not b -> sum(coeffs .* xs) <= bound` over floats. */
predicate fzn_float_lin_le_imp_not(
  list of float: coeffs,
  list of var float: xs,
  float: bound,
  var bool: b,
) =
  let {
    float: hi = mip_float_lin_ub(coeffs, xs);
  } in if is_fixed(b) then
    if fix(b) then true else fzn_float_lin_le(coeffs, xs, bound) endif
  elseif hi <= bound then
    true
  elseif mip_float_lin_lb(coeffs, xs) > bound then
    b % It can never fail.
  else
    assert(
      hi < infinity,
      "This model needs a finite upper bound on `\(coeffs) . \(xs)` for a big-M " ++
        "constraint, but at least one variable in it is unbounded. Give the " ++
        "variables in the model finite domains.",
    ) /\
      fzn_float_lin_le(
        coeffs ++ [-(hi - bound)],
        xs ++ [int2float(bool2int(b))],
        bound,
      )
  endif;

/** @group mip `not b -> sum(coeffs .* xs) < bound` over floats. */
predicate fzn_float_lin_lt_imp_not(
  list of float: coeffs,
  list of var float: xs,
  float: bound,
  var bool: b,
) = fzn_float_lin_le_imp_not(coeffs, xs, bound - MIP_float_epsilon, b);

/** @group mip `b -> sum(coeffs .* xs) < bound` over floats.
 *
 * Shifting the bound by `MIP_float_epsilon` does **not** preserve equivalence:
 * a solution within that of the bound is lost.
 */
predicate fzn_float_lin_lt_imp(
  list of float: coeffs,
  list of var float: xs,
  float: bound,
  var bool: b,
) = fzn_float_lin_le_imp(coeffs, xs, bound - MIP_float_epsilon, b);

/** @group mip `not b -> sum(coeffs .* xs) = total` over floats. */
predicate fzn_float_lin_eq_imp_not(
  list of float: coeffs,
  list of var float: xs,
  float: total,
  var bool: b,
) =
  if is_fixed(b) then
    if fix(b) then true else fzn_float_lin_eq(coeffs, xs, total) endif
  else
    fzn_float_lin_le_imp_not(coeffs, xs, total, b) /\
      fzn_float_lin_le_imp_not([-c | c in coeffs], xs, -total, b)
  endif;

/** @group mip Whether a two-valued `x` takes its upper value.
 *
 * A `var 0..1` is the Boolean itself, so the backend holds them in one column
 * and the constraint costs nothing at all. Worth its own case because a
 * flattened model is full of them, and the equality encoding below would spend
 * two binaries and three rows on each.
 */
predicate mip_two_valued(var int: x, var bool: b) =
  if lb(x) = 0 /\ ub(x) = 1 then
    fzn_bool_to_int(b, x)
  else
    fzn_int_lin_eq([1, -(ub(x) - lb(x))], [x, bool2int(b)], lb(x))
  endif;

/** @group mip The indicator for a two-valued `x` taking its upper value.
 *
 * A function, and `promise_total`, so that every constraint asking about the
 * same `x` shares one indicator and the one row that channels it. Calling
 * `mip_two_valued` directly gives each caller its own, which a decomposition
 * comparing every *pair* of something pays for quadratically.
 */
function var bool: mip_two_valued_at(var int: x) :: promise_total =
  let {
    var bool: b;
    constraint mip_two_valued(x, b);
  } in b;

/** @group mip Whether a two-valued `x` takes its *lower* value.
 *
 * `mip_two_valued` with the polarity in the coefficient, so that a caller
 * needing the negated sense does not have to build `not b`.
 */
predicate mip_two_valued_not(var int: x, var bool: b) =
  fzn_int_lin_eq([1, ub(x) - lb(x)], [x, bool2int(b)], ub(x));

/** @group mip One Boolean per value in `x`'s domain, true exactly for the
 * value `x` takes.
 *
 * The indicators are Booleans rather than `linear/`'s `var 0..1` columns, which
 * a MIP backend holds as binary columns either way.
 */
function array [int] of var bool: mip_select(var int: x) :: promise_total =
  let {
    array [int] of var bool: p =
      array1d(lb(x)..ub(x), [mip_select_bit(x, i) | i in lb(x)..ub(x)]);
    constraint
      fzn_bool_lin_eq([1 | i in index_set(p)], [p[i] | i in index_set(p)], 1);
    constraint
      if card(dom(x)) = 2 then
        % Two values: the indicator for the upper one already determines `x`, so
        % the weighted sum would say the same thing a second time — and over a
        % `var 0..1` it says it for no row at all.
        mip_two_valued(x, p[ub(x)])
      else
        % An expression, not `fzn_bool_lin_eq(..., x)`: `x` in an argument
        % position has to be a variable, so a caller whose index is itself a
        % linear expression — a chain step's `2 * xb + yb + 1` — pays a row to
        % name it. Written as an equality between two sums, the flattener folds
        % that definition into this row.
        sum (i in index_set(p)) (i * bool2int(p[i])) = x
      endif;
  } in p;

function var bool: mip_select_bit(var int: x, int: i) :: promise_total =
  if i in dom(x) then let { var bool: b } in b else false endif;

/** @group mip The positions of the terms that are not fixed.
 *
 * The rewrite layer states `x = y` as `[1, -1] . [x, y]`, so a comparison
 * against a constant reaches this library as two terms, one of them fixed.
 * Recognising the single-variable forms means folding those away first.
 */
function array [int] of int: mip_free_terms(array [int] of var int: xs) =
  [i | i in index_set(xs) where not is_fixed(xs[i])];

/** @group mip What the fixed terms contribute to the left-hand side. */
function int: mip_fixed_sum(array [int] of int: coeffs, array [int] of var int: xs) =
  sum (i in index_set(xs) where is_fixed(xs[i])) (coeffs[i] * fix(xs[i]));

/** @group mip One Boolean per pair of values `x` and `y` can take together.
 *
 * Exactly one holds, and each single-value indicator is the row (or column) of
 * pairs it appears in — so this refines `mip_select` rather than replacing it.
 */
function array [int, int] of var bool: mip_select_pair(var int: x, var int: y) :: promise_total =
  let {
    array [int] of var bool: px = mip_select(x);
    array [int] of var bool: py = mip_select(y);
    array [int, int] of var bool: pair =
      array2d(
        index_set(px),
        index_set(py),
        [mip_pair_bit(x, i, y, j) | i in index_set(px), j in index_set(py)],
      );
    constraint
      fzn_bool_lin_eq(
        [1 | i in index_set(px), j in index_set(py)],
        [pair[i, j] | i in index_set(px), j in index_set(py)],
        1,
      );
    constraint
      forall (i in index_set(px)) (
        fzn_bool_lin_eq(
          [1 | j in index_set(py)] ++ [-1],
          [pair[i, j] | j in index_set(py)] ++ [px[i]],
          0,
        )
      );
    constraint
      forall (j in index_set(py)) (
        fzn_bool_lin_eq(
          [1 | i in index_set(px)] ++ [-1],
          [pair[i, j] | i in index_set(px)] ++ [py[j]],
          0,
        )
      );
  } in pair;

function var bool: mip_pair_bit(var int: x, int: i, var int: y, int: j) :: promise_total =
  if i in dom(x) /\ j in dom(y) then let { var bool: p } in p else false endif;

/** @group mip The indicator for `x = value`, or `false` when `x` cannot take it.
 *
 * `mip_select` is `promise_total`, so several values of one variable share one
 * encoding.
 */
function var bool: mip_select_at(var int: x, int: value) :: promise_total =
  let {
    array [int] of var bool: p = mip_select(x);
  } in if value in index_set(p) then p[value] else false endif;
predicate mip_set_in_encoded(var int: x, set of int: si, var bool: b, bool: iff) =
  assert(
    card(dom(x)) <= MIP_max_equality_encoding,
    "This model asks whether \(x) lies in a set of \(card(si)) values scattered " ++
      "through a domain of \(card(dom(x))), which is linear only through an " ++
      "encoding of that domain. Raise `MIP_max_equality_encoding`, or give the " ++
      "variable a smaller domain.",
  ) /\
    (
      if iff then
        fzn_bool_lin_eq(
          [1 | v in si] ++ [-1],
          [mip_select_at(x, v) | v in si] ++ [b],
          0,
        )
      else
        fzn_bool_lin_le(
          [-1 | v in si] ++ [1],
          [mip_select_at(x, v) | v in si] ++ [b],
          0,
        )
      endif
    );

% `x` avoids every member of `s`, which is the negated form `set_in_reif` needs
% when its indicator is already false.
predicate mip_int_not_in(var int: x, set of int: s) =
  forall (v in s intersect dom(x)) (fzn_int_lin_ne([1], [x], v));

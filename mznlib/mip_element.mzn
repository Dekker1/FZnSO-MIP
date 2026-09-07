% Reading an array at a variable index: one binary per reachable position, and
% an implication per position. A fixed array collapses to one row, because the
% selector binaries can carry the values as coefficients.

include "mip_linear.mzn";
include "fzn_bool_clause.mzn";
include "fzn_int_in.mzn";
include "fzn_bool_clause_reif.mzn";

/** @group mip The position in the flat array that `index` selects.
 *
 * Only the n-dimensional forms need this. A one-dimensional lookup selects on
 * `index` itself: rebasing through a fresh variable would give that variable
 * its *own* selector array, so an index read by both a lookup and a comparison
 * — a graph model's `parent[n]`, read by `dist[parent[n]]` and by
 * `parent[n] != n` — would be encoded twice.
 *
 * Declared over the index's own domain rebased, not the array's extent: it is
 * what decides how many selector binaries `mip_select` creates.

 *
 */
function var int: mip_position(int: size, int: offset, var int: index) =
  let {
    % The image of the index's *domain*, not of its bounds. Rebasing through a
    % range would hand `mip_select` a binary for every position between the
    % first and the last, where a holey index is exactly the case that makes the
    % encoding worth narrowing.
    set of int: reachable =
      if has_bounds(index) then
        {i + 1 - offset | i in dom(index)} intersect 1..size
      else
        1..size
      endif;
    var reachable: position;
    constraint fzn_int_lin_eq([1, -1], [position, index], 1 - offset);
  } in position;

/** @group mip The positions that could produce a value `value` can take.
 *
 * Narrowing the index first is what decides how big everything below is: an
 * encoding costs a binary per *reachable* position, so a position that can
 * never be selected must not reach one.
 */
function set of int: mip_live_positions(list of var int: xs, var int: value) =
  if has_bounds(value) then
    {i | i in index_set(xs) where card(dom(xs[i]) intersect dom(value)) > 0}
  else
    index_set(xs)
  endif;

predicate mip_int_element(list of var int: xs, int: offset, var int: index, var int: value) =
  let {
    set of int: live = mip_live_positions(xs, value);
  } in if card(live) = 0 then
    false
  else
    % Stated over the caller's numbering, so it narrows `index` itself rather
    % than a rebased copy of it.
    fzn_int_in(index, {i + offset - 1 | i in live}) /\
      if forall (i in live) (is_fixed(xs[i])) then
        mip_int_element_fixed([fix(xs[i]) | i in live], live, offset, index, value)
      elseif card(live) = 2 then
        % Two positions need one Boolean saying which, not a selector array: the
        % array costs a second binary and the row tying the two together.
        % `linear/`'s `array_var_int_element` makes the same cut.
        let {
          var bool: upper = mip_two_valued_at(index);
        } in fzn_int_lin_eq_imp([1, -1], [value, xs[max(live)]], 0, upper) /\
          fzn_int_lin_eq_imp_not([1, -1], [value, xs[min(live)]], 0, upper)
      else
        forall (i in live) (
          fzn_int_lin_eq_imp(
            [1, -1],
            [value, xs[i]],
            0,
            mip_select_at(index, i + offset - 1),
          )
        )
      endif
  endif;

% A constant array, where the shape of the values often removes the need for an
% encoding altogether. `linear/`'s `array_int_element` makes the same three cuts.
predicate mip_int_element_fixed(
  array [int] of int: vs,
  set of int: live,
  int: offset,
  var int: index,
  var int: value,
) =
  let {
    array [int] of int: v = array1d(live, vs);
    int: lo = min(live);
    int: hi = max(live);
  } in if min(v) = max(v) then
    % Every reachable position holds the same value.
    value = min(v)
  elseif live = lo..hi /\
    forall (i in lo..hi - 1) (v[i + 1] - v[i] = v[lo + 1] - v[lo]) then
    % An arithmetic progression: the value is an affine function of the index,
    % which is one row and no encoding at all.
    let {
      int: step = v[lo + 1] - v[lo];
    } in fzn_int_lin_eq([1, -step], [value, index], v[lo] - step * (lo + offset - 1))
  else
    fzn_bool_lin_eq(
      [v[i] | i in live],
      [mip_select_at(index, i + offset - 1) | i in live],
      value,
    )
  endif;

predicate mip_float_element(
  list of var float: xs,
  int: offset,
  var int: index,
  var float: value,
) =
  let {
    array [int] of var bool: at =
      array1d(
        index_set(xs),
        [mip_select_at(index, i + offset - 1) | i in index_set(xs)],
      );
  } in if forall (i in index_set(at)) (is_fixed(xs[i])) then
    % The `bool2int` and `int2float` cost no columns: both are total functions
    % between two decisions, which the backend holds as one.
    fzn_float_lin_eq(
      [fix(xs[i]) | i in index_set(at)] ++ [-1.0],
      [int2float(bool2int(at[i])) | i in index_set(at)] ++ [value],
      0.0,
    )
  else
    forall (i in index_set(at)) (
      fzn_float_lin_eq_imp([1.0, -1.0], [value, xs[i]], 0.0, at[i])
    )
  endif;

predicate mip_bool_element(list of var bool: xs, int: offset, var int: index, var bool: value) =
  let {
    array [int] of var bool: at =
      array1d(
        index_set(xs),
        [mip_select_at(index, i + offset - 1) | i in index_set(xs)],
      );
    set of int: live = {i | i in index_set(at) where not is_fixed(at[i]) \/ fix(at[i])};
    set of int: yes = {i | i in live where is_fixed(xs[i]) /\ fix(xs[i])};
    set of int: no = {i | i in live where is_fixed(xs[i]) /\ not fix(xs[i])};
  } in if live = yes union no then
    % Every element is a constant, so the result holds exactly when the selected
    % position is one of the true ones: one reified clause rather than two
    % clauses per position.
    fzn_bool_clause_reif([at[i] | i in yes], [], value)
  else
    % A constant element does not need its own pair of clauses either. At most
    % one selector is true, so one row over all the `true` elements says "if the
    % selected element is one of these, so is `value`", and one over the `false`
    % ones says the converse — two rows for the whole constant part, however
    % much of the array it is.
    mip_bool_element_selected(yes, at, value, 0) /\
      mip_bool_element_selected(no, at, value, 1) /\
      forall (i in live diff (yes union no)) (
        % `at[i] -> (value <-> xs[i])`, as the two clauses it is.
        fzn_bool_clause([value], [at[i], xs[i]]) /\
          fzn_bool_clause([xs[i]], [at[i], value])
      )
  endif;

/** @group mip `at[i] for i in sel` forces `value` to `1 - polarity`.
 *
 * At most one selector is true, so the implication is one row rather than one
 * clause per position — but only worth a row once there are two of them.
 */
predicate mip_bool_element_selected(
  set of int: sel,
  array [int] of var bool: at,
  var bool: value,
  int: polarity,
) =
  if card(sel) = 0 then
    true
  elseif card(sel) = 1 then
    fzn_bool_clause(
      [if polarity = 0 then value else at[min(sel)] endif],
      [if polarity = 0 then at[min(sel)] else value endif],
    )
  else
    fzn_bool_lin_le(
      [1 | i in sel] ++ [if polarity = 0 then -1 else 1 endif],
      [at[i] | i in sel] ++ [value],
      polarity,
    )
  endif;

/** @group mip The position in a row-major flat array that `indices` selects.
 *
 * One linear row, because the strides are constants.
 */
function var int: mip_position_nd(
  int: size,
  array [int] of int: offsets,
  array [int] of int: sizes,
  array [int] of var int: indices,
) =
  let {
    int: d = length(indices);
    array [1..d] of int: stride = [product (e in i + 1..d) (sizes[e]) | i in 1..d];
    var 1..size: position;
    constraint
      fzn_int_lin_eq(
        [1] ++ [-stride[i] | i in 1..d],
        [position] ++ [indices[i] | i in 1..d],
        1 - sum (i in 1..d) (stride[i] * offsets[i]),
      );
  } in position;

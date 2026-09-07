% Lexicographic comparison, as a pair of chains: one binary per position for
% "equal here" and one for "less here", which together read off the first
% position where the arrays differ.
%
% The chain rows are over binary columns and need no big-M; only the element
% comparisons do, and for Boolean arrays not even those.
%
% `strict` says whether running out of `xs` before `ys` counts as a difference:
% a prefix is already at most, so `lesseq` is strict only when `xs` is longer,
% and `less` unless the arrays run out together.

include "mip_linear.mzn";

% A prefix already decides the comparison, so the chain need only be forced to
% break within it when running out of `xs` first would leave `xs` the greater.
test mip_lex_strict(int: nx, int: ny, bool: strict) =
  if strict then nx >= ny else nx > ny endif;

% Neither array has a common position, so one of them is empty.
test mip_lex_empty(int: nx, int: ny, bool: strict) =
  nx = 0 /\ (ny > 0 \/ not strict);

% The `equal` and `less` chains, and the rows tying them together.
predicate mip_lex_chain(array [int] of var bool: equal, array [int] of var bool: less,
                        bool: strict) =
  let {
    set of int: positions = index_set(equal);
    int: first = min(positions);
  } in
    fzn_bool_lin_eq([1, 1], [equal[first], less[first]], 1) /\
    forall (i in positions where i > first) (
      fzn_bool_lin_eq([1, -1, -1], [equal[i - 1], equal[i], less[i]], 0)
    ) /\
    (if strict then
      fzn_bool_lin_eq([1 | i in positions], [less[i] | i in positions], 1)
    else
      fzn_bool_lin_le([1 | i in positions], [less[i] | i in positions], 1)
    endif);

predicate mip_int_lex(list of var int: xs, list of var int: ys, bool: strict) =
  let {
    int: size = min(length(xs), length(ys));
  } in if size = 0 then
    mip_lex_empty(length(xs), length(ys), strict)
  else
    let {
      array [1..size] of var bool: equal;
      array [1..size] of var bool: less;
      constraint
        mip_lex_chain(equal, less, mip_lex_strict(length(xs), length(ys), strict));
    } in forall (i in 1..size) (
      fzn_int_lin_eq_imp([1, -1], [xs[i], ys[i]], 0, equal[i]) /\
        fzn_int_lin_le_imp([1, -1], [xs[i], ys[i]], -1, less[i])
    )
  endif;

predicate mip_float_lex(list of var float: xs, list of var float: ys, bool: strict) =
  let {
    int: size = min(length(xs), length(ys));
  } in if size = 0 then
    mip_lex_empty(length(xs), length(ys), strict)
  else
    let {
      array [1..size] of var bool: equal;
      array [1..size] of var bool: less;
      constraint
        mip_lex_chain(equal, less, mip_lex_strict(length(xs), length(ys), strict));
    } in forall (i in 1..size) (
      fzn_float_lin_eq_imp([1.0, -1.0], [xs[i], ys[i]], 0.0, equal[i]) /\
        fzn_float_lin_lt_imp([1.0, -1.0], [xs[i], ys[i]], 0.0, less[i])
    )
  endif;

predicate mip_bool_lex(list of var bool: xs, list of var bool: ys, bool: strict) =
  let {
    int: size = min(length(xs), length(ys));
  } in if size = 0 then
    mip_lex_empty(length(xs), length(ys), strict)
  else
    let {
      array [1..size] of var bool: equal;
      array [1..size] of var bool: less;
      constraint
        mip_lex_chain(equal, less, mip_lex_strict(length(xs), length(ys), strict));
    } in forall (i in 1..size) (
      if is_fixed(xs[i]) /\ is_fixed(ys[i]) then
        % Both entries known, so both implications below resolve: a difference
        % here rules out `equal[i]`, and anything but a step up rules out
        % `less[i]`. Neither is ever forced *true* — `equal[i]` claims the whole
        % prefix matches and `less[i]` that this is the position that breaks it,
        % and only the chain knows that. A symmetry-breaking matrix with a
        % pinned row is mostly these.
        (if fix(xs[i]) = fix(ys[i]) then true else not equal[i] endif) /\
          (if fix(ys[i]) /\ not fix(xs[i]) then true else not less[i] endif)
      else
        % Rows over binary columns, so a Boolean array needs no big-M at all.
        fzn_bool_lin_le([1, -1, 1], [xs[i], ys[i], equal[i]], 1) /\
          fzn_bool_lin_le([-1, 1, 1], [xs[i], ys[i], equal[i]], 1) /\
          fzn_bool_lin_le([1, 1], [xs[i], less[i]], 1) /\
          fzn_bool_lin_le([1, -1], [less[i], ys[i]], 0)
      endif
    )
  endif;

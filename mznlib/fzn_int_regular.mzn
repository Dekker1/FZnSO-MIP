% `linear/fzn_regular.mzn`, in registry spelling.
%
% The standard decomposition states `a[i + 1] = d[a[i], xs[i]]` as an element
% lookup per position, which a MIP pays for as a two-dimensional element. Both
% formulations below work over the equality encodings instead: the transition is
% a relation between the value indicators, which is already linear.
%
% Narrowing the state and symbol domains first is not an optimisation. The
% encodings are one binary per value, so every state the automaton cannot reach
% would otherwise be a column and a row.

include "options.mzn";
include "mip_linear.mzn";

predicate fzn_int_regular(
  list of var int: xs,
  int: states,
  int: alphabet,
  int: alphabet_offset,
  list of int: transition,
  int: initial,
  set of int: accepting,
) =
  if length(xs) = 0 then
    initial in accepting
  else
    let {
      int: n = length(xs);
      set of int: symbols = alphabet_offset..alphabet_offset + alphabet - 1;
      % State 0 is the failing state, which is why it is excluded below rather
      % than given an encoding.
      array [1..states, symbols] of int: dd = array2d(1..states, symbols, transition);
      array [1..n + 1] of var 1..states: a;
      constraint a[1] = initial;
      constraint a[n + 1] in accepting;
      % Forwards: a state is reachable only from the states before it.
      constraint
        forall (i in 1..n) (
          xs[i] in symbols /\
            a[i + 1] in {dd[va, vx] | va in dom(a[i]), vx in dom(xs[i])} diff {0}
        );
      % Backwards: a state or symbol that leads nowhere is not worth encoding.
      constraint
        forall (i in [n + 1 - k | k in 1..n]) (
          a[i] in
            {va | va in dom(a[i]) where exists (vx in dom(xs[i])) (dd[va, vx] in dom(a[i + 1]))} /\
            xs[i] in
              {vx |
                vx in dom(xs[i]) where exists (va in dom(a[i])) (dd[va, vx] in dom(a[i + 1]))}
        );
    } in forall (i in 1..n) (
      if card(dom(a[i])) * card(dom(xs[i])) > MIP_max_regular_flow then
        mip_regular_step_implied(a[i], xs[i], a[i + 1], dd)
      else
        mip_regular_step_flow(a[i], xs[i], a[i + 1], dd)
      endif
    )
  endif;

% One row per transition: taking `from = va` and reading `vx` forces the state
% the automaton moves to. Larger than the flow form, but linear in the pairs
% rather than materialising one binary for each.
predicate mip_regular_step_implied(
  var int: from,
  var int: symbol,
  var int: to,
  array [int, int] of int: dd,
) =
  forall (va in dom(from), vx in dom(symbol)) (
    if dd[va, vx] in dom(to) then
      fzn_bool_lin_le(
        [1, 1, -1],
        [mip_select_at(from, va), mip_select_at(symbol, vx), mip_select_at(to, dd[va, vx])],
        1,
      )
    else
      % No transition, so the two cannot both hold.
      fzn_bool_lin_le([1, 1], [mip_select_at(from, va), mip_select_at(symbol, vx)], 1)
    endif
  );

% The network-flow formulation of [Côté, Gendron & Rousseau]: one binary per
% (state, symbol) pair, and the state reached is the flow into it.
predicate mip_regular_step_flow(
  var int: from,
  var int: symbol,
  var int: to,
  array [int, int] of int: dd,
) =
  let {
    array [int, int] of var bool: pair = mip_select_pair(from, symbol);
  } in forall (va2 in dom(to)) (
    fzn_bool_lin_eq(
      [1 | va1 in dom(from), vx in dom(symbol) where dd[va1, vx] == va2] ++ [-1],
      [pair[va1, vx] | va1 in dom(from), vx in dom(symbol) where dd[va1, vx] == va2] ++
        [mip_select_at(to, va2)],
      0,
    )
  ) /\
    forall (
      va1 in dom(from),
      vx in dom(symbol) where not (dd[va1, vx] in dom(to)),
    ) (not pair[va1, vx]);

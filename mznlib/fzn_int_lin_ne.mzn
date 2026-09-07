% `sum(coeffs .* xs) != total`.
%
% A disequality is a disjunction, which is the one place a fresh binary is
% unavoidable: it says which side of `total` the sum falls on. Two big-M rows,
% rather than the four a reified equality would cost.

include "mip_linear.mzn";
include "fzn_int_lin_eq_reif.mzn";

predicate fzn_int_lin_ne(list of int: coeffs, list of var int: xs, int: total) =
  if mip_eq_reif_is_single(coeffs, xs) then
    let {
      array [int] of int: free = mip_free_terms(xs);
      var int: x = xs[free[1]];
      int: v = coeffs[free[1]] * (total - mip_fixed_sum(coeffs, xs));
    } in if not (v in dom(x)) then
      true
    elseif card(dom(x)) = 2 then
      % Ruling out one of two values fixes the other.
      x = (if v = lb(x) then ub(x) else lb(x) endif)
    elseif mip_domains_wanted(x) then
      int_ne__POST(x, v)
    elseif v = ub(x) then
      % Ruling out an endpoint is a bound, not a disjunction: one row and no
      % binary. `linear/` tests this before handing the constraint to
      % `MIPdomains`, which it can afford to because it states the bound as
      % `x <= v - 1` and the pass absorbs an `int_le` into the domain it is
      % already building. A registry row is opaque to the pass, so here the
      % bound would be paid for on top of that domain rather than instead of it.
      x <= v - 1
    elseif v = lb(x) then
      x >= v + 1
    elseif card(dom(x)) <= MIP_max_equality_encoding then
      % The equality encoding's indicator held at zero: one row, no binary.
      not mip_select_at(x, v)
    else
      fzn_int_lin_ne__general(coeffs, xs, total)
    endif
  else
    fzn_int_lin_ne__general(coeffs, xs, total)
  endif;

predicate fzn_int_lin_ne__general(list of int: coeffs, list of var int: xs, int: total) =
  let {
    var bool: below;
  } in fzn_int_lin_le_imp(coeffs, xs, total - 1, below) /\
    fzn_int_lin_le_imp_not([-c | c in coeffs], xs, -total - 1, below);

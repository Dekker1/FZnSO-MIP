% `b <-> sum(coeffs .* xs) <= bound`, as the implication and its converse.
%
% Two big-M rows. The converse is the same predicate on the negated row: not
% `<= bound` is `>= bound + 1` for integers, which is `-sum <= -bound - 1`.
%
% A comparison of one variable against a constant goes to `MIPdomains` instead,
% which encodes it against whatever it already holds for that variable. The
% bound checks below are not an optimisation: the pass assumes every value it is
% given lies within the variable's domain, and asserts internally if one does
% not.

include "mip_linear.mzn";

predicate fzn_int_lin_le_reif(
  list of int: coeffs,
  list of var int: xs,
  int: bound,
  var bool: b,
) =
  if mip_eq_reif_is_single(coeffs, xs) /\ MIP_domains /\ not is_fixed(b) /\
    mzn_in_root_context() then
    let {
      array [int] of int: free = mip_free_terms(xs);
      var int: x = xs[free[1]];
      int: rest = bound - mip_fixed_sum(coeffs, xs);
      % `x <= v` when the coefficient is 1, `x >= v` when it is -1.
      int: v = coeffs[free[1]] * rest;
    } in if coeffs[free[1]] = 1 then
      if ub(x) <= v then
        b = true
      elseif lb(x) > v then
        b = false
      else
        % Snap the bound onto a value `x` can actually take. `linear/` does the
        % same, and it is not cosmetic: the pass splits the domain at every
        % value it is given, so a bound in a hole costs an interval — and the
        % intervals are what its inner loops are linear in.
        int_le_reif__POST(x, max(dom(x) intersect -infinity..v), bool2int(b))
      endif
    else
      if lb(x) >= v then
        b = true
      elseif ub(x) < v then
        b = false
      else
        int_ge_reif__POST(x, min(dom(x) intersect v..infinity), bool2int(b))
      endif
    endif
  else
    fzn_int_lin_le_imp(coeffs, xs, bound, b) /\
      fzn_int_lin_le_imp_not([-c | c in coeffs], xs, -bound - 1, b)
  endif;


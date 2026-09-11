% `a = b * q + r` with `sign(r) = sign(a)` and `|r| < |b|`, as
% `linear/redefinitions.mzn` writes it. Division and modulo share every
% constraint but the result they name. `b * q` is a variable product, which is
% where this gets expensive.

include "mip_linear.mzn";
include "fzn_int_times.mzn";
include "fzn_int_abs.mzn";
include "fzn_int_lin_le_reif.mzn";

% Total, which is what lets a backend with indicator or quadratic rows use them
% here. Division is partial in `b = 0`, but that is settled above: the standard
% library reaches `fzn_int_div` and `fzn_int_mod` only through `div_t`, which is
% itself `promise_total` and posts `b != 0` at root (`div_mt` maps `0` to `1`),
% so `b` is non-zero by the time this is called — and `magnitude` below states
% `|b| >= 1` in any case.
%
% Without it the constraints in this `let` are reified wherever the enclosing
% expression is, and a native `fzn_int_lin_le_imp` or `fzn_int_times` has no
% reified form to reify into, so the model does not flatten at all.
function array [int] of var int: mip_int_divmod(var int: a, var int: b)
  :: promise_total =
  let {
    set of int: quotients =
      if lb(b) * ub(b) > 0 then
        let {
          set of int: corners = {
            ub(a) div ub(b),
            ub(a) div lb(b),
            lb(a) div ub(b),
            lb(a) div lb(b),
          };
        } in min(corners)..max(corners)
      else
        let { int: reach = max(abs(lb(a)), abs(ub(a))) } in -reach..reach
      endif;
    var quotients: quotient;
    int: reach = max(abs(lb(b)), abs(ub(b)));
    var -reach + 1..reach - 1: remainder;
    constraint
      if is_fixed(b) then
        % A fixed divisor makes `b * quotient` a term rather than a product, so
        % the identity is one row and needs nothing to name the product by.
        fzn_int_lin_eq([fix(b), 1, -1], [quotient, remainder, a], 0)
      else
        let {
          % Needs a domain of its own: every formulation of a product is
          % bounded.
          set of int: products = {
            lb(b) * min(quotients),
            lb(b) * max(quotients),
            ub(b) * min(quotients),
            ub(b) * max(quotients),
          };
          var min(products)..max(products): scaled;
          constraint fzn_int_times(b, quotient, scaled);
        } in fzn_int_lin_eq([1, -1, -1], [a, scaled, remainder], 0)
      endif;
    constraint fzn_int_lin_le_imp([-1], [remainder], 0, mip_nonneg(a));
    constraint fzn_int_lin_le_imp_not([1], [remainder], 0, mip_nonneg(a));
    var 1..reach: magnitude;
    constraint fzn_int_abs(b, magnitude);
    constraint fzn_int_lin_le([-1, 1], [magnitude, remainder], -1);
    constraint fzn_int_lin_le([-1, -1], [magnitude, remainder], -1);
  } in [quotient, remainder];

% Reified once, so the two sign implications share one binary.
function var bool: mip_nonneg(var int: a) :: promise_total =
  let {
    var bool: nonneg;
    constraint fzn_int_lin_le_reif([-1], [a], 0, nonneg);
  } in nonneg;

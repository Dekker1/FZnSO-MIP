% `b <-> sum(coeffs .* xs) != total`.
%
% Not `fzn_int_lin_eq_reif(..., not b)`: `not b` on a variable is a second
% Boolean and a row tying it to the first, which is what keeping `var bool`
% costs where `linear/` gets `1 - b` folded into a coefficient for nothing. Both
% forms below name `b` itself.

include "mip_linear.mzn";
include "mip_domains.mzn";
include "fzn_int_lin_le_reif.mzn";
include "fzn_int_lin_ne.mzn";

predicate fzn_int_lin_ne_reif(
  list of int: coeffs,
  list of var int: xs,
  int: total,
  var bool: b,
) =
  if is_fixed(b) then
    if fix(b) then
      fzn_int_lin_ne(coeffs, xs, total)
    else
      fzn_int_lin_eq(coeffs, xs, total)
    endif
  elseif mip_eq_reif_is_single(coeffs, xs) then
    mip_int_ne_reif_single(coeffs, xs, total - mip_fixed_sum(coeffs, xs), b)
  else
    mip_int_lin_ne_reif__general(coeffs, xs, total, b)
  endif;

% `b <-> coeffs[j] * x != rest`, for the one free term `j`.
predicate mip_int_ne_reif_single(
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
    b
  elseif card(dom(x)) = 2 then
    % Ruling out one of two values selects the other.
    if v = ub(x) then mip_two_valued_not(x, b) else mip_two_valued(x, b) endif
  elseif mip_domains_encodes(x) then
    % The pass is going to encode this variable anyway, so the encoding below
    % would be the second one.
    mip_int_ne_reif_posted(x, v, b)
  elseif card(dom(x)) <= MIP_max_equality_encoding then
    % The value's own indicator, held opposite to `b`: one row against an
    % encoding the variable's other constraints share.
    fzn_bool_lin_eq([1, 1], [mip_select_at(x, v), b], 1)
  else
    mip_int_lin_ne_reif__general([1], [x], v, b)
  endif;

% At least one of `<= total` and `>= total` always holds, so the two of them
% sum to 2 exactly when the equality does — and `b` is what is left over.
predicate mip_int_lin_ne_reif__general(
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
  } in fzn_bool_lin_eq([1, 1, 1], [le, ge, b], 2);

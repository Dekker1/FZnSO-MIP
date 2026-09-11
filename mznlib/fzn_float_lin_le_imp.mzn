% The registry constraint `float_lin_le_imp`, as one big-M row.
%
% Its own file so that a solver layer can replace just this one: a backend
% with indicator constraints ships a declaration with no body, and the row
% below is never built. See `../README.md`.

include "mip_linear.mzn";

/** @group mip `b -> sum(coeffs .* xs) <= bound` over floats. */
predicate fzn_float_lin_le_imp(
  list of float: coeffs,
  list of var float: xs,
  float: bound,
  var bool: b,
) =
  let {
    float: hi = mip_float_lin_ub(coeffs, xs);
  } in if is_fixed(b) then
    if fix(b) then fzn_float_lin_le(coeffs, xs, bound) else true endif
  elseif hi <= bound then
    true
  elseif mip_float_lin_lb(coeffs, xs) > bound then
    not b % It can never hold.
  else
    assert(
      hi < infinity,
      "This model needs a finite upper bound on `\(coeffs) . \(xs)` for a big-M " ++
        "constraint, but at least one variable in it is unbounded. Give the " ++
        "variables in the model finite domains.",
    ) /\
      fzn_float_lin_le(
        coeffs ++ [hi - bound],
        xs ++ [int2float(bool2int(b))],
        hi,
      )
  endif;

% There is no linear formulation of exponentiation, so the powers are a constant
% table and this is a 2-D element lookup.
%
% `pow(x, y)` is left unguarded: a negative exponent is *undefined* rather than
% zero, and substituting a value here would answer a model asking for `0 ^ -1`
% instead of leaving MiniZinc's relational semantics to decide.

include "mip_linear.mzn";

% The table runs over the bounds, so it holds pairs the domains exclude. A
% negative power of zero is undefined, and evaluating `pow` for such a pair
% fails the whole constraint even when neither argument can take it. The table
% carries a placeholder there, and the guard below rules the pair out, which is
% what the undefinedness means: `a = 0` with a negative `b` has no answer.

predicate fzn_int_pow(var int: a, var int: b, var int: c) =
  let {
    array [int, int] of int: table =
      array2d(
        lb(a)..ub(a),
        lb(b)..ub(b),
        [
          if x = 0 /\ y < 0 then
            0
          else
            pow(x, y)
          endif |
            x in lb(a)..ub(a),
            y in lb(b)..ub(b),
        ],
      );
  } in (
    if 0 in dom(a) /\ lb(b) < 0 then
      a = 0 -> b >= 0
    else
      true
    endif
  ) /\
    fzn_int_lin_eq([1, -1], [c, table[a, b]], 0);

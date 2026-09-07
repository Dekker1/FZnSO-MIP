% There is no linear formulation of exponentiation, so the powers are a constant
% table and this is a 2-D element lookup.
%
% `pow(x, y)` is left unguarded: a negative exponent is *undefined* rather than
% zero, and substituting a value here would answer a model asking for `0 ^ -1`
% instead of leaving MiniZinc's relational semantics to decide.

include "mip_linear.mzn";

predicate fzn_int_pow(var int: a, var int: b, var int: c) =
  let {
    array [int, int] of int: table =
      array2d(
        lb(a)..ub(a),
        lb(b)..ub(b),
        [pow(x, y) | x in lb(a)..ub(a), y in lb(b)..ub(b)],
      );
  } in fzn_int_lin_eq([1, -1], [c, table[a, b]], 0);

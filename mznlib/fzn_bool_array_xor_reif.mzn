% `b <-> (an odd number of `xs` hold)`.
%
% `xs` together with `b` then always hold an *even* number, so this is one row
% and one variable — the same shape as `fzn_bool_array_xor`, with `b` simply
% joining the sum. Reifying the parity any other way costs a Boolean per
% element.
%
% Worth having because the rewrite layer sends `bool_xor(x, y, b)` here: stated
% as `fzn_bool_array_xor([x, y, not b])` instead, every disequality between two
% Booleans pays for materialising `not b` — a variable and a row — and a set
% disequality does that once per member.

% A fixed element is folded out first, exactly as in `fzn_bool_array_xor`: it
% only flips which parity `xs` together with `b` must have, and what is left is
% often the two of them, where one parity is an equality the compiler resolves
% without a row.

include "mip_linear.mzn";
include "fzn_bool_array_xor.mzn";

predicate fzn_bool_array_xor_reif(list of var bool: xs, var bool: b) =
  let {
    set of int: live = {i | i in index_set(xs) where not is_fixed(xs[i])};
    int: held = sum (i in index_set(xs) where is_fixed(xs[i])) (bool2int(fix(xs[i])));
  } in mip_bool_parity([xs[i] | i in live] ++ [b], held mod 2);

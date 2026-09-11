% A product of two variables is not linear, so this is `linear/`'s cascade of
% special cases in registry spelling, ending in one row per value of the
% narrower operand.
%
% As in `fzn_float_times.mzn`, there is no branch for a backend that has
% quadratic rows: declaring `int_times` replaces this body, and naming
% `int_times` here would only come back to this predicate through the rewrite
% layer.

include "mip_linear.mzn";
include "mip_element.mzn";
include "fzn_int_pow.mzn";

predicate fzn_int_times(var int: a, var int: b, var int: c) =
  if is_fixed(a) then
    fzn_int_lin_eq([1, -fix(a)], [c, b], 0)
  elseif is_fixed(b) then
    fzn_int_lin_eq([1, -fix(b)], [c, a], 0)
  elseif is_same(a, b) then
    fzn_int_pow(a, 2, c)
  elseif dom(a) = 0..1 /\ dom(b) = 0..1 then
    % `c = a /\ b` over 0/1 integers.
    fzn_int_lin_le([1, 1, -1], [a, b, c], 1) /\
      fzn_int_lin_le([-1, 1], [a, c], 0) /\
      fzn_int_lin_le([-1, 1], [b, c], 0)
  elseif not (has_bounds(a) /\ has_bounds(b)) then
    % Every remaining formulation enumerates a domain, so an unbounded one has
    % none to offer.
    abort(
      "Unable to create a linear formulation of `int_times(\(a), \(b), \(c))`.\n" ++
        "\tA product of two variables is linear only over finite domains, and at\n" ++
        "\tleast one of these has none. Give the variables in the model finite\n" ++
        "\tdomains, or use a solver that declares `int_times`.",
    )
  else
    % The narrower operand indexes: one row per value of it, against its own
    % equality encoding. Enumerating the *pairs* instead costs
    % `|dom(a)| * |dom(b)|` binaries and both encodings, and is never cheaper.
    if card(dom(a)) > card(dom(b)) then
      fzn_int_times(b, a, c)
    else
      mip_int_times_element(a, b, c)
    endif
  endif;

% `a = value -> c = value * b`, one row per value of `a`.
%
% Not an element lookup over an array of `value * b`: materialising that array
% costs an introduced variable and a defining equation per value, where folding
% the constant into the row's own coefficient costs neither.
predicate mip_int_times_element(var int: a, var int: b, var int: c) =
  forall (value in dom(a)) (
    fzn_int_lin_eq_imp([1, -value], [c, b], 0, mip_select_at(a, value))
  );

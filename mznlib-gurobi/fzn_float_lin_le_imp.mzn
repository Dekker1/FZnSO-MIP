% `b -> sum(coeffs .* xs) <= bound`, which Gurobi posts as an indicator
% constraint (`GRBaddgenconstrIndicator`).
%
% No body: the solver implements it, so the big-M row and the binary column in
% `../mznlib/fzn_float_lin_le_imp.mzn` are never built. This is the largest
% single saving the interface offers a backend that has indicators.
%
% The parameter names match `fzn_float_lin_le`, which is not cosmetic: a call is
% re-resolved to its half-reified form by matching the base predicate's
% parameter names, so a disagreement leaves the reification unfindable.

predicate fzn_float_lin_le_imp(
  list of float: coeffs,
  list of var float: xs,
  float: bound,
  var bool: b,
);

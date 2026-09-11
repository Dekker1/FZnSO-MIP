% `float_times`, which Gurobi posts as a quadratic row (`GRBaddqconstr`).
%
% No body: the solver implements it, so the call goes over the interface
% unchanged and the cascade in `../mznlib/fzn_float_times.mzn` — an element encoding
% one row wide per value of the narrower operand — is never read.

predicate fzn_float_times(var float: a, var float: b, var float: c);

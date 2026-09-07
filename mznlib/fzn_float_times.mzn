% A fixed operand makes this linear; otherwise it is genuinely quadratic and a
% backend without quadratic rows cannot express it at all. Any "linear"
% formulation over continuous variables would be a different constraint.

include "options.mzn";
include "mip_linear.mzn";

predicate fzn_float_times(var float: a, var float: b, var float: c) =
  if is_fixed(a) then
    fzn_float_lin_eq([1.0, -fix(a)], [c, b], 0.0)
  elseif is_fixed(b) then
    fzn_float_lin_eq([1.0, -fix(b)], [c, a], 0.0)
  elseif MIP_quadratic_float then
    % The FlatZinc builtin, whose rewrite is dropped for a backend that
    % declares `float_times`; see `fzn_int_times.mzn` for why that is not a
    % loop.
    float_times(a, b, c)
  else
    abort(
      "Unable to create a linear formulation of `float_times(\(a), \(b), \(c))`.\n" ++
        "\tThis instance needs a quadratic constraint, which the selected solver\n" ++
        "\tdoes not have. Pass `-D QuadrFloat=true` if it does, or use integer\n" ++
        "\tvariables.",
    )
  endif;

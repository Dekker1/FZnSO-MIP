% The `__POST` protocol, which hands a domain-related constraint to the
% flattener's `MIPdomains` pass instead of encoding it here.
%
% The pass runs in every compile pass unless `--no-MIPdomains`. It gathers the
% variables these calls name, builds one encoding per variable — an equality
% encoding, or a decomposition into the intervals the constraints cut the domain
% into — emits `int_lin_eq` / `int_lin_le` rows for it, and removes the calls.
% One encoding then serves every constraint on that variable, which is the whole
% point: encoding each reified comparison separately pays for a fresh pair of
% big-M rows every time.
%
% Two conditions, and missing either is silent:
%
%   - **All sixteen must be declared.** `registerPOSTConstraintDecls` returns
%     false on the first one missing and the pass then does nothing at all, so a
%     partial set is worse than none.
%   - **The first argument must be a variable.** A call whose first argument has
%     been folded to a literal is skipped *and left in place*, so it would reach
%     the solver as a constraint it never declared. `mip_domains_wanted` is the
%     guard.
%
% The pass also assumes every value it is handed lies inside the variable's
% domain, and asserts internally if one does not — so a caller settles the
% out-of-range cases itself before calling, as `fzn_int_lin_le_reif` does.
%
% Turn it off with `-D fMIPdomains=false`, which stops these calls being made.
% MiniZinc's own `--no-MIPdomains` stops the *pass* but not the calls, and they
% would then reach the solver.
%
% Every indicator is a `var int`: the pass was written for `linear/`, where a
% Boolean is already a 0/1 column. Behind this interface it is not, so the calls
% pass `bool2int(b)` — which the backend holds in the same column as `b`, so the
% conversion costs nothing.

include "options.mzn";
include "fzn_bool_lin_eq.mzn";

% Whether to hand this constraint to the pass rather than encode it here.
test mip_domains_wanted(var int: x) = MIP_domains /\ not is_fixed(x);
test mip_domains_wanted(var float: x) = MIP_domains /\ not is_fixed(x);

% Whether the pass will build an equality encoding of `x` whatever this library
% does. It encodes every variable whose domain has a hole, and no `__POST` call
% is needed to make it: a hole is itself a domain constraint as far as it is
% concerned. So encoding such a variable here too puts *two* encodings of it in
% front of the solver, and the caller should hand the constraint over instead.
test mip_domains_encodes(var int: x) =
  mip_domains_wanted(x) /\ has_bounds(x) /\ card(dom(x)) != ub(x) - lb(x) + 1;

/** @group mip `b <-> x = v`, against the encoding the pass builds.
 *
 * The `__POST` call names `b` itself, so it costs no row; the caller pays only
 * for the negation where it wanted the disequality.
 */
predicate mip_int_eq_reif_posted(var int: x, int: v, var bool: b) =
  int_eq_reif__POST(x, v, bool2int(b));

/** @group mip `b <-> x != v`, against the encoding the pass builds. */
predicate mip_int_ne_reif_posted(var int: x, int: v, var bool: b) =
  let { var bool: eq } in
    int_eq_reif__POST(x, v, bool2int(eq)) /\ fzn_bool_lin_eq([1, 1], [eq, b], 1);

predicate int_le_reif__POST(var int: x, int: y, var int: b);
predicate int_ge_reif__POST(var int: x, int: y, var int: b);
predicate int_eq_reif__POST(var int: x, int: y, var int: b);
predicate int_ne__POST(var int: x, int: y);

predicate float_le_reif__POST(var float: x, float: y, var int: b, float: epsRel);
predicate float_ge_reif__POST(var float: x, float: y, var int: b, float: epsRel);
predicate float_eq_reif__POST(var float: x, float: y, var int: b, float: epsRel);
predicate float_ne__POST(var float: x, float: y, float: epsRel);
predicate aux_float_lt_zero_iff_1__POST(var float: x, var int: b, float: epsRel);

% The half-reified forms, reached only under `MIP_domains_aux`.
predicate aux_int_le_zero_if_1__POST(var int: x, var int: p);
predicate aux_float_eq_zero_if_1__POST(var float: x, var int: pI, var float: p);
predicate aux_float_le_zero_if_1__POST(var float: x, var int: pI, var float: p);
predicate aux_float_lt_zero_if_1__POST(var float: x, var int: pI, var float: p, float: eps);

% Declared, never called, and the reason is measured rather than assumed.
%
% `linear/` hands the pass its own encoding here so that it reuses it. That
% works only where the rows *defining* the encoding are `int_lin_eq`, the one
% linear form the pass reads: `mip_select` defines its binaries with
% `fzn_bool_lin_eq`, so the pass holds an encoding whose definition it cannot
% see and `syncWithEqEncoding` asserts. Defining them with `int_lin_eq` over
% `bool2int` views instead does fix that — and makes the models *bigger*
% (`nvalue_total` 142 rows to 252, `github_674` 796 to 835), because the pass
% then rebuilds against an encoding this library had already specialised. So
% the handover loses either way.
predicate equality_encoding__POST(var int: x, array [int] of var int: x_eq_d);

predicate set_in__POST(var int: x, set of int: s__);
predicate set_in_reif__POST(var int: x, set of int: s__, var int: b);

% The flags this library branches on. A backend sets them from its `.msc`, a
% model with `-D`; the defaults are what a plain linear MIP needs, so a backend
% that adds nothing sets nothing.
%
% The `-D` names are `linear/`'s, so an existing invocation keeps working. The
% values derived from them are `MIP_`-prefixed.

% Holes are the backend's job: `src/mip_model.cpp` gives a column of several
% intervals one binary per interval, where making the flattener carve them costs
% a binary per value. `linear/` sets this false too, but pairs it with
% `MIPdomains.cpp` — which needs all seventeen `__POST` predicates declared, and
% silently does nothing if one is missing.
mzn_opt_only_range_domains = false;

% MIPdomains: hand a domain-related constraint to the flattener's own pass
% rather than encode it here, so that one encoding per variable serves every
% constraint on it. `mip_domains.mzn` says what it costs to get wrong.
opt bool: fMIPdomains;
bool: MIP_domains = if absent(fMIPdomains) then true else deopt(fMIPdomains) endif;

% Handing the pass a reified *difference* `x - y <= k`, by naming the
% difference, is `linear/`'s `fPostproDom_DIFF`. Tried and measured worse on
% every model: `cardinality_atmost_partition` 309 rows to 441,
% `pred_param_r7550` 6,310 to 9,752. Two big-M rows beat a named difference plus
% the encoding the pass then builds for it, so there is no flag for it here.

% The half-reified `__POST` forms. Off, as in `linear/`.
opt bool: fMIPdomAux;
bool: MIP_domains_aux = if absent(fMIPdomAux) then false else deopt(fMIPdomAux) endif;

% Quadratic rows. Only a backend declaring `Capabilities::quadratic` may turn
% these on: without the declaration the native call rewrites straight back into
% the decomposition it came from.
opt bool: QuadrFloat;
bool: MIP_quadratic_float = if absent(QuadrFloat) then false else deopt(QuadrFloat) endif;

opt bool: QuadrInt;
opt int: QuadrIntCard;
% Take the native product once the smaller domain has this many values.
int: MIP_quadratic_int_card =
  if occurs(QuadrIntCard) then
    deopt(QuadrIntCard)
  elseif occurs(QuadrInt) /\ deopt(QuadrInt) then
    0
  else
    infinity
  endif;

% Nothing below reads these yet: each belongs to a decomposition still to be
% ported (`fzn_int_circuit`, `fzn_int_regular`, the `lex` family) or to a native
% predicate only a `<solver>.mzn` can name, which a shared file cannot. They are
% kept so that a backend author finds `linear/`'s names rather than inventing
% new ones — and so the library does not quietly become HiGHS-shaped.

opt int: nSECcuts; % 0: MTZ; 1, 2: hand circuits to the cut generator
int: MIP_sec_cuts = if absent(nSECcuts) then 0 else deopt(nSECcuts) endif;

opt bool: UseOrbisack;
bool: MIP_orbisack = if absent(UseOrbisack) then false else deopt(UseOrbisack) endif;

opt bool: UseOrbitope;
bool: MIP_orbitope = if absent(UseOrbitope) then false else deopt(UseOrbitope) endif;

% A native cumulative over fixed durations and requirements, as SCIP has.
opt bool: UseCumulative;
bool: MIP_cumulative_fixed =
  if absent(UseCumulative) then false else deopt(UseCumulative) endif;

% Caps on the encodings that grow with a domain rather than with the model.
int: MIP_max_cumulative_span = 2000;
int: MIP_max_equality_encoding = 2000;
int: MIP_max_regular_flow = 20000; % unused until `fzn_int_regular` is ported

% A strict float inequality is the non-strict one shifted by this. It does
% **not** preserve equivalence — a solution within `eps` of the bound is lost —
% which is why the float tests compare approximately.
opt float: float_EPS;
float: MIP_float_epsilon = if absent(float_EPS) then 1e-6 else deopt(float_EPS) endif;

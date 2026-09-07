# The shared MIP library

One `fzn_<registry-ident>.mzn` per constraint a MIP backend cannot post itself,
plus the handful of `mip_*.mzn` files the decompositions are written in terms
of. It is shared by every backend in `../src/`; nothing here is HiGHS-specific
except `highs.mzn`, which is empty because HiGHS has no extensions.

There is **no `redefinitions*.mzn`**. The rewrite layer in
`share/minizinc/fznso_rewrite/` owns the FlatZinc builtins, and a second library
defining one is a duplicate definition rather than an override. What a solver
library overrides is `fzn_<ident>`.

## What the backend takes natively

Nine constraints, and every one of them is **one row or one column property**:

| | |
| --- | --- |
| `int_lin_eq`, `int_lin_le` | one row |
| `float_lin_eq`, `float_lin_le` | one row |
| `bool_lin_eq`, `bool_lin_le` | one row over binary columns |
| `bool_clause` | one row over binary columns |
| `bool_to_int`, `int_to_float` | **no row** — the two decisions share a column |

That is the line between a native post and a reformulation, and everything on
the other side of it is in this directory.

## `_imp` is the building block

`linear/redefs_lin_halfreifs.mzn` is an `aux_*` zoo — `aux_int_le_zero_if_0`,
`aux_int_le_if_1`, `aux_int_gt_if_0` and sixteen derived forms — and every one
of them takes a `var int`, because that library channels every `var bool`
through `bool2int` into a `var 0..1` column before it starts. What those
predicates *are* is half reification, and behind this interface a `var bool`
already **is** a binary column, so they are written that way instead:

    fzn_int_lin_le_imp      one big-M row
    fzn_int_lin_eq_imp   =  the two inequalities
    fzn_int_lin_le_reif  =  the imp form and its converse
    fzn_int_lin_eq_reif  =  1 + b = le + ge
    fzn_int_lin_ne       =  one fresh binary, two imp rows

The big-M row is the one place a Boolean genuinely enters an arithmetic sum, so
it is the one place a `bool2int` survives — as a plain call, which the rewrite
layer lowers to `fzn_bool_to_int` and the backend turns into *the same column*
through the aliasing pre-pass in `src/mip_model.cpp`. `bool2int` is
deliberately **not** overridden; `linear/`'s `is_reverse_map` channel is exactly
the layer this replaces.

## Flags

`options.mzn`, trimmed from `linear/options.mzn` to what is still a choice. The
`-D` names are `linear/`'s, so an existing invocation keeps working; the values
derived from them are `MIP_`-prefixed rather than `MZN__`.

**Domains with holes are the backend's job**, which is why
`mzn_opt_only_range_domains` stays false. A MIP column is an interval, so a
domain of several needs saying in rows — and `src/mip_model.cpp` says it with
one binary per interval and three rows, where asking the flattener to carve the
holes instead builds an equality encoding: a binary per *value*, and eleven rows
on `var {1, 9}` where two do. `MIPdomains` takes the ones it wants first; the
backend is what catches the rest.

**`MIPdomains` is on**, through the sixteen `__POST` declarations in
`mip_domains.mzn`. It is a flattener pass, so this path gets it on the same
terms `linear/` does: a reified comparison of one variable against a constant is
handed over rather than encoded here, and one encoding per variable then serves
every constraint on it. Missing a single declaration turns the whole pass off
silently, and so does handing it a call whose first argument has folded to a
literal — that one is left in place and reaches the solver.

It is on for a second reason that is not a choice: **the pass encodes a variable
whose domain has a hole whether it is asked to or not.** A hole is a domain
constraint as far as it is concerned, and no `__POST` call is needed to make it
act. So for such a variable the question is not whether there will be an
encoding but whether there will be *two*, and `mip_domains_encodes` is the test
that sends the comparison to the pass rather than encoding it here again. On
`globals_global_cardinality_low_up_closed` that is 19 rows against 11, and 11 is
`linear/`'s own.

`QuadrInt` / `QuadrFloat` route `fzn_int_times` and `fzn_float_times` onto the
FlatZinc builtin, whose rewrite `enable_native_predicates` has dropped for a
backend that declares the constraint. A backend that does *not* declare it would
loop straight back, which is why the flags default to off and only a backend
declaring `Capabilities::quadratic` turns them on.

## What is not here, and why

- **`fzn_*_imp` is never reached from the flattener.** MiniZinc derives an
  `_imp` rewrite only for names in the solver's declared list, and a file here
  is read only when something includes it — the one always included is
  `redefinitions.mzn`, which this library must not ship. So the `_imp`
  predicates are internal building blocks and a half-reified context falls back
  to `_reif`, which is what the flattener would have done anyway.

- **Indicator constraints have no gate.** For the same reason: the flag branch
  would have to name a predicate, and the only registry-conforming name loops
  back through this library. `Capabilities::indicators` still adds the `_imp`
  forms to the declared list, which is the ABI-visible half; a backend with
  indicators additionally needs a `fzn_int_lin_le_imp.mzn` of its own, because
  MiniZinc does not drop bodies in a solver's own `mznlib`.

- **Cut-generator globals.** `circuit__SECcuts`, `fzn_lex_lesseq__orbisack` and
  `fzn_lex_chain_lesseq__orbitope` reach a *solver-specific* predicate, which a
  shared file cannot name. The flags they are driven by are in `options.mzn`;
  the bodies belong in the `<solver>.mzn` of a backend that has a cut callback.
  No backend here does.

- **`fzn_lex_chain_lesseq_orbitope` and `circuit__SECcuts`.** Both reach a
  *solver-specific* predicate, which a shared file cannot name. The flags that
  would select them are in `options.mzn`; the bodies belong in the
  `<solver>.mzn` of a backend with a cut generator, and no backend here has one.
  `fzn_int_circuit` asserts rather than silently ignoring `MIP_sec_cuts`.

  Everything else in `linear/` is ported. `int_cumulative` was the one that
  mattered most: the standard decomposition reifies an overlap per pair of
  tasks, which on `unit/regression/pred_param_r7550.mzn` cost 282,410
  constraints against the reference's 6,310, and `linear/`'s time-indexed
  formulation brings it to 8,033.

- **`equality_encoding__POST`.** Declared, never called. `linear/` hands the
  pass its own encoding so it reuses it, but that works only where the rows
  *defining* the encoding are `int_lin_eq` — the one linear form the pass reads.
  `mip_select` defines its binaries with `fzn_bool_lin_eq`, because they are
  Booleans rather than 0/1 columns, so the pass cannot see the encoding that is
  already there.

  Handing `mip_select`'s binaries over and letting the pass state the rows is
  smaller — `test_circuit` 80 rows to 64, under `linear/`'s own 72 — and
  **wrong**: that model then answers with `[2, 1, 1, 1]`, which is not a
  circuit, and `array_set_element_nosets` reads a two-element set out of an
  array of singletons. Writing the row over `bool2int` terms so the pass adopts
  it instead is sound but much larger, because the Boolean bound propagation in
  `optimize.cpp` reads a row over Booleans and not one over `bool2int`:
  `github_674` 129 rows to 179 and `bug335` 583 to 605, measured before other
  work took `github_674` to 23.

  What is used instead is `mip_domains_encodes`: the pass encodes a variable
  whose domain has a hole whether or not it is asked to, so a *reified
  comparison* against such a variable is handed over rather than encoded here,
  and the pass's encoding serves it. `fzn_int_lin_eq_reif` and
  `fzn_int_lin_ne_reif` are where that branch is; the second is what
  `var opt int` reaches, through `opt_internal_int`'s `b <-> y != 0`.

- **The float and `set_in` `__POST` forms.** Declared, not yet called. The
  integer ones are where the measured gap was.

- **`fzn_bool_lin_eq_reif` is here rather than in `fznso_constraints/`.**
  The `_ne` and `_le` reified forms of `bool_lin_*` are there and this one is
  not, and a reified context needs it: the equality encoding states itself with
  `fzn_bool_lin_eq`, and MiniZinc only reifies a call whose reified form is
  declared. `mip_linear.mzn` includes the file for that reason alone.

- **The transcendentals and the CP-only reified globals.** `float_sin`,
  `float_exp`, `float_pow` and the reified graph, mdd and regular constraints
  abort. `linear/` cannot do those either, so leaving them aborting is the
  honest answer rather than a decomposition that is not one.

## Where `-Glinear` still decomposes smaller

The bar this library was written to is that no model reaches the solver with
more rows than MiniZinc's own `-Glinear` gives it, measured with
`scripts/compare-decompositions.sh` on the same locally built MiniZinc. Over the
830 models both sides compile, 19 are larger and 137 are smaller, and the 19
hold 45 rows between them — nothing above six, and eleven of them are a single
row.

Every one of the 33 falls into one of the six causes below, and
none of them is a stray `bool_to_int` — the backend holds a Boolean and its
integer in one column, so those are free on both sides and the comparison
excludes them. `scripts/attribute.py` takes any of these apart; its header says
how, and names the two traps that have each produced a confident wrong answer.

**1. The `var opt` and `element` decompositions, a row or two at a time.**
`opt_internal_int`'s `b <-> y != 0`, the `if ... elseif ...` chain over
optionals, and an element read through them. No single frame dominates any of
these models: `github_683` is 20 rows against 14 spread over five frames at one
or two rows each. This is the biggest group left and the least concentrated —
and the reason to be careful reading it, because a frame can differ by far more
than the model does. `bug335` charges 100 rows to one `int_eq` where the
reference charges 20 there and 80 to `int_eq_reif`: the same hundred rows, and
its real difference is five spread over three other frames.

**2. `fznso_nosets/` materialises a set that only the output reads.** The layer
rewrites every set variable into bits at flatten time, including one no
constraint mentions. `-Glinear` leaves such a set out of the FlatZinc entirely
and rebuilds it in the `.ozn` from the index. `unit/general/test_par_set_element.mzn`
is one row against none: the set is `a[x]` for a constant array `a`, and the row
is what ties its bits to `x`.

**3. MIPdomains' second encoding, where the value's indicator is read
directly.** The pass encodes a variable whose domain has a hole whether it is
asked to or not, so `mip_domains_encodes` hands a reified *comparison* over to
it rather than encoding here again — see **Flags**. `fzn_int_all_different`
reads a value's indicator without a comparison, so it has nothing to hand over,
and that variable ends up with two encodings. `test_circuit` used to be 80 rows
against 72 for this reason; the second optimizer pass below now folds enough of
the duplicate away to take it to 56, under the reference, but the duplication
is still built. Handing `mip_select`'s own binaries over closes it properly and
is unsound; see the `equality_encoding__POST` note above.

**4. Float `min`/`max` cannot narrow its candidates.** The integer form drops
*variables* that cannot attain the extremum, which takes `globals_maximum_int`
to no rows at all. The float form goes only as far as the constants — collapsing
them to the one that can win keeps every bound the rows state about the result —
because narrowing further breaks MiniZinc's bounds inference for `min_t`, which
reads the result's bounds back out of what the decomposition says about it.
`test-globals-float` is 11 rows against 5.

**5. A `var bool` is not a term.** Where `-Glinear` folds something into a
coefficient, holding the Boolean costs a row:

- `lex` merges the two sides into the difference `x - y` over `var 0..1`
  columns; the difference is not a term here, so it is five rows per position
  against four.
- the `if ... elseif ...` chain's "no earlier condition held" is a coefficient
  there and a row here — `globals_strictly_increasing_opt`, 12 against 10.
- a negation feeding a `bool2int` that a linear row reads: `github_700`, 6
  against 4. Not a systematic loss, though — across the models that still
  decompose larger the reference emits *more* negation rows than this library
  does, 70 against 61.

This one is the interface's premise rather than a defect: see "the one idea" in
the repository's `CLAUDE.md`.

**6. Not a spelling gap.** The obvious suspicion — that MiniZinc's own
propagation branches key on the *builtin* ids while this library's rows are
`fzn_`-spelled — was checked and is wrong: every branch in `optimize.cpp` that
tests a builtin linear or clause id has the registry name beside it. Where the
reference briefly gained more from a pass than this library did, the cause was
each time a library one, and named above.

**7. Two models where the reference is the one that is wrong.** They are counted
against this library and should not be:

- `unit/compilation/most_specific_reif.mzn` declares `my_pred` and never defines
  it. `-Glinear` drops all three calls and emits the Boolean structure alone;
  this library passes them through as the native constraints they are, so the
  solver rejects the model — which is the honest answer, and four rows worse.
- `unit/regression/github_683.mzn` ends in `constraint c = 1`. `-Glinear`
  answers `c = 2`; this library answers `c = 1` with one of the two solutions
  the test lists. Ten rows worse, for being right.


### Every model that is still larger, and which of the six it is

| rows | model | cause |
| --- | --- | --- |
| +6 | `test-globals-float` | 4 — float `min`/`max`, two of them |
| +6 | `github_683` | 1 — five frames at one or two rows |
| +5 | `bug335` | mostly 7 — see below; three real frames at +3, +3, −3 |
| +4 | `opt_minmax` | 4 — the float extremum, plus the float `if_then_else` |
| +4 | `most_specific_reif` | 7 — the reference drops three undeclared calls |
| +3 | `github_700` | 5 — `card` of a set is a row here, a term there |
| +2 | `github_669` | 1 |
| +2 | `array_set_element_nosets` | 2 — a set only the output reads |
| +2 | `globals_strictly_increasing_opt` | 5 — the `if ... elseif` chain's "no earlier condition held" |
| +2 | `test_negated_let_good_2` | 1 |
| +1 | `github_997` | mostly 7 — `arg_max` is charged +179 against a real +1 |
| +1 | `github_700_bad_sol` | 5 — one negation row |
| +1 | `github_668` | 1 |
| +1 | `bug109` | mostly 7 — `int_lin_eq` against `int_lin_eq_reif` |
| +1 | `globals_value_precede_chain_int_opt` | mostly 7 — +9 and +6 against −2 and −2 |
| +1 | `test_bool_lex_lesseq` | 5 — `lex`, five rows per position against four |
| +1 | `test_bool_lex_less` | 5 — the same |
| +1 | `globals_inverse` | 1 |
| +1 | `test_par_set_element` | 2 — a set only the output reads |

**7. Read the frames, not the frame totals.** Attribution charges a row to the
last frame the two compilations share, and the two can charge the *same* rows to
different names. `bug335` shows +80 on one `int_eq` where the reference charges
20 there and 80 to `int_eq_reif` — the same hundred rows, and its real
difference is five spread over three other frames. `github_997` shows +179 for a
model that differs by one. Always check that the frame differences sum to the
model's.

### One thing deliberately not done

`-Glinear` emits duplicate rows — 32 of them on `unit/regression/ts_bug.mzn`,
where this library emits one. A pass that folds a row again after unification
and drops exact duplicates removes them from both sides. It was written,
measured and **reverted**: it is worth one row to this library and about thirty
to the reference, so all it does is make the yardstick stricter without making
the encoding better. It also moved `ts_bug` from level to +24, because the
reference's duplicates had been masking a real 24-row difference there — the
`nosets` element promotion of causes 2, which two attempts failed to close.

## `oldflatzinc` runs the optimizer again

`optimize()` runs *before* the conversion to old FlatZinc, and that conversion
turns every `is_defined_var` declaration into a row — an alias written as
`x - y = k`, an equivalence written as the two clauses keeping `var bool` makes
of it, a clause already down to one literal. Those rows used to be the one thing
no pass had ever seen. `oldflatzinc` now takes an `optimizeAfterConversion` flag
and runs `optimize()` once more, in the middle of itself: after the rows exist
and before anything is compacted, because the optimizer needs the item numbering
and the occurrence lists it was built against. The occurrence lists are rebuilt
from scratch first — the new rows were never counted, and a variable one of them
is the only user of would otherwise look dead, leaving a row that names
something undeclared.

It is worth about a sixth of the suite's remaining excess, and both this library
and `linear/` get it: `test_circuit` 80 rows to 56, `test_count_set` 71 to 51,
`cardinality_atmost_partition` 16 to 0, `abs_bug` 48 to 42.

## Known limitations

**A big-M needs a finite bound.** A reified or half-reified comparison against a
variable with no finite domain has no MIP encoding, and this library says so:

    This model needs a finite upper bound on `[1, -1] . [x, 7]` for a big-M
    constraint, but at least one variable in it is unbounded.

`-Glinear` has the same limitation and reaches it less often, because its
`set_in` narrows a domain before anything reifies against it — under
`b -> (x in S)` neither can, since the implication says nothing about `x`. Give
the variable a domain. The same goes for a product of two variables, which every
linear formulation gets by enumerating a domain.

**A functional `int_times` can fail inside MiniZinc.**
`unit/regression/enigma_1568.mzn`, which writes `num1 * num2` in a value
position, ends in `"undeclared function or predicate int_times"` — an internal
error by MiniZinc's own label. It happens only when `fzn_int_times.mzn` here
carries a decomposition, yet the predicate is never entered, so it is a bug in
the functional-call reconstruction rather than anything this library can write
around. `unit/compilation/aggregation.mzn` reaches a MiniZinc internal error on
this path independently.

**`MIP_lazy` and `MIP_cut` are not declared.** `linear/options.mzn` gets away
with declaring them because `redefinitions.mzn` — which MiniZinc always includes
— pulls that file in, and this library must not ship one. They are solver
vocabulary anyway: a backend with a cut callback declares them in its own
`<solver>.mzn`.

## The loop trap

Bodies here are **not** dropped by `enable_native_predicates` — that only
applies to the standard library's own directories — so this is the one place a
rewrite loop can hide, and the symptom is that the flattener does not return,
with no error printed.

Every body therefore names an `fzn_` predicate explicitly. Writing `x = y` or
`sum(...) <= k` would flatten back to `int_eq` or `int_lin_le`, which the
rewrite layer sends straight back here.

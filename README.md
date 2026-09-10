# A MIP framework behind FZnSO

A reusable core for mixed-integer programming solvers behind the FZnSO
interface, with HiGHS as the first backend. `MIPWrapper` in libminizinc already
proves the shape — six solvers behind one interface and one shared MiniZinc
library — and this keeps it: **`src/` and `mznlib/` contain nothing
solver-specific**. Adding Gurobi is one subdirectory under `src/`, one
`Capabilities` value and one `.msc`.

## Layout

| | |
| --- | --- |
| `src/mip_backend.hh` | what a backend supplies, and what it says it can do |
| `src/mip_model.*` | the model walk: aliasing pre-pass, columns, rows, objective |
| `src/mip_solver.*` | `fznso::Solver` — options, statistics, solutions, stopping |
| `src/signatures.*` | the five capability lists, derived from `Capabilities` |
| `src/highs/` | the backend and the export |
| `src/test/` | the self-test, run against each built backend |
| `mznlib/` | the shared linear library — see its own README |

## The one idea in the core

**`bool_to_int` and `int_to_float` are not rows. They are the same column.**

Both are total functions between two decisions, so in a MIP the two decisions
hold one value. Posting an equality row instead would leave a redundant column
per reification for presolve to clean up, and every big-M row in `mznlib/`
converts a Boolean exactly once — so the saving is not incidental.

`MipModel` therefore makes two passes over the constraint list. The first
unions the arguments of every such conversion between two decisions in a
union-find; the second posts everything else, skipping what the first consumed.
A column's kind is the strongest among its members — any integer member makes it
integer — and its domain is the intersection of theirs.

## The other one

**A column is an interval, and a domain need not be.** `decision_domain` is a
range list, so `var {1, 9}` is two intervals and reading only its bounds is a
silent wrong answer: a model whose objective points into the gap comes back
with `x = 5`.

So a column of several intervals gets one binary per interval, exactly one of
which holds, and is held between the interval that one selects — three rows,
whatever the domain's width. The alternative is to make the flattener carve the
holes instead (`mzn_opt_only_range_domains`), which costs a binary per *value*;
see `mznlib/options.mzn` for why that is not what this does.

## Building

```
cmake -S . -B build
cmake --build build
ctest --test-dir build -C Debug
```

HiGHS is linked in, not loaded at run time: an installed one is used if
`find_package(HIGHS)` finds it, and otherwise it is fetched and built here, so a
checkout needs nothing but a compiler and a network. The built library therefore
carries no HiGHS dependency of its own.

The FZnSO name of a solver is its library's base name, so the result is
`build/lib/fznso/<config>/libhighs.dylib` — the same leaf as HiGHS's own shared
library. The `fznso` directory keeps them apart; Gecode is in the same
position.

## What HiGHS declares

Nine constraints, every one a single row or column property; three decision
types, no `var set of int`, which is what puts `fznso_nosets/` on the
flattener's include path. See `mznlib/README.md` for where the line is drawn and
`src/signatures.cpp` for the list itself.

`cargo run -p fznso-conform -- check build/lib/fznso/Debug/libhighs.dylib`
checks it against the registry.

## Incremental runs

An instance is run any number of times, and two things carry between runs.

**The model.** `layer_unchanged()` says how many layers are exactly as the
solver last saw them, and index order follows layer order, so what a retracted
layer took with it is always a *suffix* of the columns and rows. A run therefore
truncates the backend to the last unchanged layer's end and posts only what lies
above it. `MipSolver` records each layer's column and row count as it builds, so
it knows where to cut; `MipBackend::truncate` does the cutting, and a backend
opts in through `Capabilities::incremental`.

Three things send it back to a full rebuild, because none can be done by adding
to what is already there:

- the objective changed — a cost is a column property, fixed when the column was
  handed over;
- a new layer's `bool_to_int` or `int_to_float` joins two decisions that already
  own a column apiece, which would mean merging two columns the backend has;
- a new layer narrows a column handed over on an earlier run.

Worth what the build costs and no more, which is proportional to the model and
independent of the search: on a 40,000-row model a re-solve spends 0.004s
building instead of 0.265s, and on `unit/regression/pred_param_r7550.mzn` — 6,310
rows against a 32-second search — it saves 0.05s of 32s.

**The last solution.** Whatever the previous run ended with is offered to the
backend as a starting point for the next, over the decisions the two runs share
— the same unchanged prefix, for the same reason. `warm_start(xs, vs)`
annotations on the objective are read as the initial hint and a carried value
overrides them, since an annotation is a guess made before anything was solved.

A start is advice: `MipBackend::set_start` may be ignored, and HiGHS tolerates
one that is infeasible. Which is just as well, because the commonest incremental
pattern — solve, add a constraint cutting off what was just found, solve again —
makes the carried solution infeasible by construction. No speedup from it has
been measured; on knapsacks of 70 and 260 items, tightening and relaxing, warm
and cold are within noise, because HiGHS's own root heuristics find an equally
good incumbent in milliseconds.

Two further ideas were investigated and are not implemented. A previous run's
*dual* bound stays valid when layers are only pushed, since the feasible set only
shrinks — but HiGHS has no MIP lower-bound input (`objective_bound` is
dual-simplex termination, `objective_target` is a stop condition). And
`Highs_getBasis`/`setBasis` cannot reach the MIP solver, which runs its own root
LP.

One thing the interface cannot carry: MiniZinc writes `warm_start_array([...])`
and puts warm starts inside `seq_search`, but an annotation argument is a
`FznsoValue` and no value kind is an annotation — so a grouped warm start is
unrepresentable, and only a flat `warm_start` written directly on the solve item
arrives here.

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

## Not incremental

Every `run` rebuilds the model from scratch and ignores `layer_unchanged`.
Always correct, just not incremental; `MipBackend::reset` is where a backend
that could reuse its matrix would hook in.

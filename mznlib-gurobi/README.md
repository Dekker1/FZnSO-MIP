# Gurobi's layer over the shared MIP library

`.msc` lists this directory before `../mznlib`, so a file here replaces the one
of the same name there and everything else falls through. What that is for is
the handful of constraints Gurobi posts *natively* where the shared library has
to decompose: an indicator constraint where the library writes a big-M row, a
quadratic row where it enumerates a domain.

Each such file declares the predicate with **no body**. A body-less declaration
is how a MiniZinc library says "the solver implements this", so the call reaches
the solver unchanged and the decomposition next door is never read. Nothing here
needs to say which solver it is — `src/gurobi/gurobi_backend.cpp` already
declares the same constraints across the interface, and `src/signatures.cpp`
derives that list from one `Capabilities` value. The two have to agree: a file
here without the matching declaration sends the solver a constraint it never
claimed.

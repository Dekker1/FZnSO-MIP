% Gurobi implements nothing beyond the constraints MiniZinc knows, so this file
% is empty. It exists because `include "gurobi.mzn";` has to resolve, and the
% flattener only synthesises one when a solver ships no library at all.
%
% What Gurobi has over HiGHS — indicator constraints, quadratic rows, a solution
% pool — needs nothing here: those are registry constraints it declares, and a
% declared constraint replaces this library's decomposition of it, including the
% decompositions in this very directory. See `src/signatures.cpp` for the list.

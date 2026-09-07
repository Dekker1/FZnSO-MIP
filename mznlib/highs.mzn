% HiGHS implements nothing beyond the constraints MiniZinc knows, so this file
% is empty. It exists because `include "highs.mzn";` has to resolve, and the
% flattener only synthesises one when a solver ships no library at all.
%
% A backend with extensions puts their `<solver>_`-prefixed declarations here.

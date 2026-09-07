% `linear/fzn_circuit.mzn`'s MTZ formulation, in registry spelling.
%
% The standard decomposition numbers the nodes with a second `all_different`
% over an order array; the Miller-Tucker-Zemlin ordering says the same thing in
% one row per arc, with the Desrochers & Laporte '91 term tightening it.
%
% Operators rather than `fzn_` names: none of `int_lin_le`, `int_eq_reif` or
% `all_different` rewrites back to this predicate, so there is no loop to close,
% and the arc terms read as the model they are.

include "options.mzn";
include "mip_linear.mzn";
include "fzn_int_all_different.mzn";

predicate fzn_int_circuit(list of var int: xs, int: offset) =
  if length(xs) = 0 then
    true
  else
    let {
      int: n = length(xs);
      set of int: nodes = offset..offset + n - 1;
      array [nodes] of var int: x = array1d(nodes, xs);
      % No self-loop, and every successor is a node: stated before the
      % `all_different` so its encoding is built over the narrowed domains.
      constraint forall (i in nodes) (x[i] in nodes diff {i});
    } in assert(
      MIP_sec_cuts = 0,
      "MIP_sec_cuts asks for subtour elimination cuts, which need a cut " ++
        "generator in the backend and a `fzn_int_circuit.mzn` of its own.",
    ) /\
      fzn_int_all_different(xs) /\
      % One order value per node but the first, and an arc `i -> j` forces
      % `order[j] = order[i] + 1`.
      let {
        array [offset + 1..offset + n - 1] of var 2..n: order;
      } in forall (i, j in offset + 1..offset + n - 1 where i != j /\ j in dom(x[i])) (
        order[i] - order[j] + (n - 1) * bool2int(x[i] == j) + (n - 3) * bool2int(x[j] == i) <=
          n - 2
      ) /\
      % A two-cycle is a subtour for `n > 2`, and ruling it out here is worth
      % more than leaving it to the ordering.
      if n > 2 then
        forall (i, j in nodes where i < j) (x[i] != j \/ x[j] != i)
      else
        true
      endif
  endif;

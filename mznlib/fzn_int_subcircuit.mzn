% `linear/fzn_subcircuit.mzn`, in registry spelling.
%
% As `fzn_int_circuit`, with a node in the circuit exactly when it does not
% point at itself, and the ordering anchored at the first such node.

include "mip_linear.mzn";
include "fzn_int_all_different.mzn";

predicate fzn_int_subcircuit(list of var int: xs, int: offset) =
  if length(xs) = 0 then
    true
  else
    let {
      int: n = length(xs);
      set of int: nodes = offset..offset + n - 1;
      int: last = offset + n - 1;
      array [nodes] of var int: x = array1d(nodes, xs);
      array [nodes] of var bool: ins = array1d(nodes, [x[i] != i | i in nodes]);
      % `last + 1` stands for "no node is in", which is what makes the minimum
      % total.
      var offset..last + 1: firstin =
        min([last + 1 + bool2int(ins[i]) * (i - last - 1) | i in nodes]);
      var nodes: lastin;
      var bool: empty = firstin == last + 1;
    } in fzn_int_all_different(xs) /\
      (empty <-> forall (i in nodes) (not ins[i])) /\
      (
        not empty <->
          order_anchored(x, ins, order_of(n, nodes), firstin, lastin, n, nodes)
      )
  endif;

% The MTZ ordering over the nodes that are in, anchored at `firstin`.
predicate order_anchored(
  array [int] of var int: x,
  array [int] of var bool: ins,
  array [int] of var int: order,
  var int: firstin,
  var int: lastin,
  int: n,
  set of int: nodes,
) =
  order[firstin] = 1 /\
    lastin > firstin /\
    x[lastin] = firstin /\
    ins[lastin] /\
    ins[firstin] /\
    forall (i, j in nodes where i != j) (
      order[i] - order[j] + n * bool2int(x[i] == j /\ i != lastin) <= n - 1
    );

function array [int] of var int: order_of(int: n, set of int: nodes) :: promise_total =
  array1d(nodes, [let { var 1..n: o } in o | i in nodes]);

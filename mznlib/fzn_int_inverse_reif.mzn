% `linear/fzn_inverse_reif.mzn`, in registry spelling.
%
% As the in-range form, with each array's values held inside the other's index
% range — which under a reification is a conjunct rather than a domain
% narrowing, since it only has to hold when `b` does.

include "mip_linear.mzn";

predicate fzn_int_inverse_reif(
  list of var int: f,
  int: f_offset,
  list of var int: invf,
  int: invf_offset,
  var bool: b,
) =
  b <->
    forall (
      i in index_set(f),
      j in index_set(invf),
    ) (
      f[i] in invf_offset..invf_offset + length(invf) - 1 /\
        invf[j] in f_offset..f_offset + length(f) - 1 /\
        ((j + invf_offset - 1 == f[i]) <-> (i + f_offset - 1 == invf[j]))
    );

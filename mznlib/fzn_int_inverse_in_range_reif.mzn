% `linear/fzn_inverse_in_range_reif.mzn`, in registry spelling.
%
% The non-reified form states one row per pair against the equality encoding;
% reified, the two memberships are what the pairs are conjoined under, so it is
% written as the conjunction it is rather than over the encoding.

include "mip_linear.mzn";

predicate fzn_int_inverse_in_range_reif(
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
      (j + invf_offset - 1 == f[i]) <-> (i + f_offset - 1 == invf[j])
    );

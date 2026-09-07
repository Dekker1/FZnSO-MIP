% As `fzn_int_inverse_in_range`, with each array's values held to the other's
% index range first — which is what gives the equality encoding its domains.

include "mip_linear.mzn";
include "fzn_int_inverse_in_range.mzn";

predicate fzn_int_inverse(
  list of var int: f,
  int: f_offset,
  list of var int: invf,
  int: invf_offset,
) =
  let {
    constraint
      forall (i in index_set(f)) (
        f[i] in invf_offset..invf_offset + length(invf) - 1
      );
    constraint
      forall (j in index_set(invf)) (
        invf[j] in f_offset..f_offset + length(f) - 1
      );
    % A second `let`, so the domains above are in place before the encoding
    % reads them.
  } in let {
    constraint fzn_int_inverse_in_range(f, f_offset, invf, invf_offset);
  } in true;

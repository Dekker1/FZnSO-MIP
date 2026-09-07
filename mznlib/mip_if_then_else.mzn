% Selecting a value by the first condition that holds. Exactly one branch is
% taken, because the last condition of an `if ... then ... else ...` is always
% `true`. The three registry forms differ only in what they select, so the
% branch selection is written once here.

include "mip_linear.mzn";

% A plain `if c then ... else ... endif` arrives as `[c, true]`. Its two branch
% indicators are `c` and its negation, so a caller states the second branch
% against `c` with the polarity flipped rather than asking here for a fresh
% Boolean and a row to tie it to `c`.
test mip_is_binary_choice(array [int] of var bool: conditions) =
  length(conditions) = 2 /\ is_fixed(conditions[max(index_set(conditions))]) /\
    fix(conditions[max(index_set(conditions))]);

function var bool: mip_choice_condition(array [int] of var bool: conditions) =
  conditions[min(index_set(conditions))];

function array [int] of var bool: mip_first_true(array [int] of var bool: conditions) =
  let {
    array [index_set(conditions)] of var bool: chosen;
    % `sum(j < i) (conditions[j]) + chosen[i] >= conditions[i]`
    constraint
      forall (i in index_set(conditions)) (
        fzn_bool_lin_le(
          [-1 | j in index_set(conditions) where j < i] ++ [-1, 1],
          [conditions[j] | j in index_set(conditions) where j < i] ++
            [chosen[i], conditions[i]],
          0,
        )
      );
    constraint
      fzn_bool_lin_eq(
        [1 | i in index_set(chosen)],
        [chosen[i] | i in index_set(chosen)],
        1,
      );
  } in chosen;

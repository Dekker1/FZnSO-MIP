% Selecting a value by the first condition that holds. With no condition
% holding the result is unconstrained, which is what the registry asks for. An
% `if ... then ... else ...` ends in a condition fixed to `true`, and then one
% branch is always taken, which is the cheaper encoding of the two below.
% The three registry forms differ only in what they select, so the branch
% selection is written once here.

include "mip_linear.mzn";
include "fzn_bool_clause.mzn";

% A plain `if c then ... else ... endif` arrives as `[c, true]`. Its two branch
% indicators are `c` and its negation, so a caller states the second branch
% against `c` with the polarity flipped rather than asking here for a fresh
% Boolean and a row to tie it to `c`.
test mip_is_binary_choice(array [int] of var bool: conditions) =
  length(conditions) = 2 /\ is_fixed(conditions[max(index_set(conditions))]) /\
    fix(conditions[max(index_set(conditions))]);

function var bool: mip_choice_condition(array [int] of var bool: conditions) =
  conditions[min(index_set(conditions))];

% Whether a branch has to be taken at all. A condition fixed to `true` says so,
% and an `else` branch is exactly that.
test mip_choice_is_total(array [int] of var bool: conditions) =
  exists (i in index_set(conditions)) (is_fixed(conditions[i]) /\ fix(conditions[i]));

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
      if mip_choice_is_total(conditions) then
        % One branch is taken, which the row above already points at.
        fzn_bool_lin_eq(
          [1 | i in index_set(chosen)],
          [chosen[i] | i in index_set(chosen)],
          1,
        )
      elseif length(conditions) = 0 then
        true
      else
        % At most one branch, and never one whose condition does not hold:
        % without the second part the solver takes a branch where the registry
        % leaves the result free.
        fzn_bool_lin_le(
          [1 | i in index_set(chosen)],
          [chosen[i] | i in index_set(chosen)],
          1,
        ) /\
          forall (i in index_set(conditions)) (
            fzn_bool_clause([conditions[i]], [chosen[i]])
          )
      endif;
  } in chosen;

% `linear/fzn_cumulative.mzn`'s formulations, in registry spelling.
%
% The standard decomposition reifies an overlap test per pair of tasks and sums
% the results, which a MIP pays for twice — once for the reification and once
% for the product. These state the resource bound as one row per time point (or
% per task), with the overlap indicator entering the sum directly.
%
% The sums are written with operators rather than `fzn_int_lin_le`, because a
% variable `resource[i]` times an overlap indicator is a product the flattener
% has to build; neither `int_le` nor `int_lin_le` rewrites back to this
% predicate, so there is no loop to close.

include "options.mzn";
include "mip_linear.mzn";

predicate fzn_int_cumulative(
  list of var int: start,
  list of var int: duration,
  list of var int: resource,
  var int: capacity,
) =
  let {
    set of int: tasks = mip_cumulative_tasks(duration, resource);
  } in if card(tasks) = 0 then
    % Nothing consumes anything, so the bound only has to be satisfiable.
    if card(index_set(start)) = 0 then true else capacity >= 0 endif
  else
    let {
      set of int: times = dom_array([start[i] | i in tasks]);
    } in if card(times) * card(tasks) <= MIP_max_cumulative_span then
      mip_cumulative_by_time(start, duration, resource, capacity, tasks, times)
    else
      mip_cumulative_by_task(start, duration, resource, capacity, tasks)
    endif
  endif;

% A task that can neither run nor consume constrains nothing.
function set of int: mip_cumulative_tasks(
  array [int] of var int: duration,
  array [int] of var int: resource,
) = {i | i in index_set(duration) where ub(resource[i]) > 0 /\ ub(duration[i]) > 0};

% One row per time point: what is running at `t` fits under the capacity.
predicate mip_cumulative_by_time(
  array [int] of var int: start,
  array [int] of var int: duration,
  array [int] of var int: resource,
  var int: capacity :: promise_ctx_monotone,
  set of int: tasks,
  set of int: times,
) =
  forall (
    t in min([lb(start[i]) | i in tasks])..max([ub(start[i] + duration[i]) | i in tasks])
      where t in times,
  ) (
    capacity >=
      sum (i in tasks) (
        resource[i] *
          (
            if is_fixed(duration[i]) then
              % A fixed duration makes the overlap a membership test, which is
              % one indicator rather than two comparisons and a conjunction.
              bool2int(start[i] in t - fix(duration[i]) + 1..t)
            else
              bool2int(start[i] <= t /\ t < start[i] + duration[i])
            endif
          )
      )
  );

% One row per task: what overlaps a task's own start fits under the capacity
% left by it. Quadratic in the tasks, but independent of the horizon.
predicate mip_cumulative_by_task(
  array [int] of var int: start,
  array [int] of var int: duration,
  array [int] of var int: resource,
  var int: capacity :: promise_ctx_monotone,
  set of int: tasks,
) =
  forall (j in tasks) (
    capacity - resource[j] >=
      sum (
        i in tasks
          where i != j /\ lb(start[i]) <= ub(start[j]) /\ lb(start[j]) < ub(start[i] + duration[i]),
      ) (resource[i] * bool2int(start[i] <= start[j] /\ start[j] < start[i] + duration[i]))
  );

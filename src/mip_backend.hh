// The MIP solver a `MipSolver` drives.
//
// One implementation per MIP solver; everything above this line is shared. The
// shape follows MiniZinc's own `MIPWrapper`, which six backends already sit
// behind — columns in one batch, then one row at a time, then solve — minus the
// bookkeeping the FZnSO core now owns.

#ifndef FZNSO_MIP_BACKEND_HH
#define FZNSO_MIP_BACKEND_HH

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "fznso.hpp"

namespace fznso_mip {

/// What a column may take.
enum class ColKind : std::uint8_t { Continuous, Integer };

/// The relation a row states between its left-hand side and its right-hand side.
enum class RowSense : std::uint8_t { Le, Eq, Ge };

/// How a solve finished, before the FZnSO status is derived from it.
enum class MipStatus : std::uint8_t {
	/// Proved optimal, or proved satisfiable with no objective.
	Optimal,
	/// A solution was found but not proved optimal.
	Feasible,
	/// Proved that no solution exists.
	Infeasible,
	/// The objective is unbounded, or unbounded-or-infeasible.
	Unbounded,
	/// A limit was hit with nothing proved.
	Unknown,
	/// The backend could not continue.
	Error,
};

/// What a backend is told before a solve, from the options the registry defines.
///
/// A backend's own options are set through `MipBackend::option_set` instead, and
/// are `<solver>_`-prefixed.
struct MipOptions {
	std::optional<std::int64_t> time_limit_ms;
	std::optional<std::int64_t> random_seed;
	std::int64_t threads = 1;
	bool verbose = false;
	bool intermediate = false;
	bool all_solutions = false;
};

/// What a backend reports back while it searches.
///
/// The core implements this; a backend calls into it from whatever callback its
/// own API provides.
class MipSink {
public:
	virtual ~MipSink() = default;

	/// One solution, as a value per column. Borrowed for the call only.
	virtual void solution(const double* col_values, std::size_t len, double objective) = 0;
	/// An improved dual bound on the objective.
	virtual void bound(double dual_bound) = 0;
	/// A line of the backend's own log output.
	virtual void log(std::string_view text) = 0;
	/// Whether the caller has asked the search to stop.
	virtual bool should_stop() = 0;
	/// Whether anything is listening to `log`, so a backend can skip producing it.
	virtual bool log_wanted() const = 0;
};

/// What a backend can do natively, which decides what the solver declares.
///
/// Each flag adds constraints to `constraint_list` or an option to
/// `option_list`; see `signatures.cpp`. A flag set here is a promise that the
/// matching `MipBackend` method is implemented.
struct Capabilities {
	/// `b -> (row)` natively, so the half-reified linear constraints are declared
	/// rather than decomposed to big-M.
	bool indicators = false;
	/// Products of two columns, so `int_times` and `float_times` are declared.
	bool quadratic = false;
	/// Enumerating solutions that share the optimal objective value.
	bool solution_pool = false;
	/// Continuous columns at all, so `var float` is a decision type.
	bool floats = true;
	/// Rows and columns can be dropped from the end of a model that has already
	/// been solved, which is what lets a run keep whatever an unchanged prefix
	/// of layers built and post only what lies above it.
	bool incremental = false;
	/// Several objectives optimised in priority order, so the `_lex_`
	/// objectives are declared rather than left to the consumer to encode as a
	/// weighted sum — which needs weights wide enough to separate the levels
	/// and loses the guarantee when they are not.
	bool lexicographic = false;
};

/// Everything one solve produced, apart from the solutions already streamed.
struct RunOutcome {
	MipStatus status = MipStatus::Unknown;
	/// Set when `status` is `Error`.
	std::string error;
	/// Whether `values` holds a feasible assignment.
	bool has_solution = false;
	double objective = 0.0;
	/// A value per column, valid until the next `solve`.
	std::vector<double> values;
	/// How many solutions the backend already streamed through the sink, so the
	/// core knows whether reporting `values` would repeat one.
	std::size_t streamed = 0;
};

/// The MIP solver behind the interface.
class MipBackend {
public:
	virtual ~MipBackend() = default;

	/// What this backend can do natively.
	virtual Capabilities capabilities() const = 0;

	/// The value the backend treats as unbounded.
	virtual double infinity() const = 0;

	/// Discard the current model, keeping option settings.
	virtual void reset() = 0;

	/// Add `n` columns at once. All four arrays have `n` entries.
	virtual void add_columns(std::size_t n, const double* obj, const double* lb, const double* ub,
	                         const ColKind* kind) = 0;

	/// Add one row `Σ val[i]·x[idx[i]]  <sense>  rhs`.
	virtual void add_row(std::size_t nnz, const int* idx, const double* val, RowSense sense,
	                     double rhs) = 0;

	/// Minimise or maximise the objective built from the column costs.
	virtual void set_objective_sense(bool maximise) = 0;

	/// Search, reporting through `sink`.
	virtual RunOutcome solve(const MipOptions& options, MipSink& sink) = 0;

	/// Set a `<solver>_`-prefixed option. Return a message to reject the value.
	virtual std::optional<std::string> option_set(std::string_view name, fznso::Value value) = 0;
	/// Read a `<solver>_`-prefixed option back, or absent if unknown.
	virtual fznso::Value option_get(std::string_view name) const = 0;

	/// A statistic this backend reports, or absent if unknown.
	virtual fznso::Value statistic(std::string_view name) const = 0;

	// --- capability-gated ---------------------------------------------------
	//
	// Reached only when the matching `Capabilities` flag is set, because the
	// core only dispatches a constraint the solver declared. The defaults throw
	// so a flag set without its method is a loud failure rather than a wrong
	// answer.

	/// `x[bin_col] = on_value  ->  Σ val[i]·x[idx[i]] <sense> rhs`.
	virtual void add_indicator_row(int bin_col, bool on_value, std::size_t nnz, const int* idx,
	                               const double* val, RowSense sense, double rhs);

	/// Optimise `x[cols[0]]`, then `x[cols[1]]` subject to it, and so on.
	///
	/// Replaces the column costs a scalar objective is built from: several
	/// objectives in priority order are a property of the model, not of one
	/// column, so `set_objective_sense` says nothing here and every cost stays
	/// zero. Called once, after the columns exist. Reached only when
	/// `Capabilities::lexicographic` is set.
	virtual void set_lex_objective(std::size_t n, const int* cols, bool maximise);

	/// `x[a_col] · x[b_col] − x[out_col] = rhs`.
	///
	/// `out_col` is negative when the product is against a value rather than a
	/// column — `x·y = 5` names no third decision — and there is then no linear
	/// term to subtract. Both operands are always columns: with either one
	/// fixed the product is linear and never reaches here.
	virtual void add_quadratic_row(int out_col, int a_col, int b_col, double rhs);

	// --- incremental --------------------------------------------------------

	/// Offer values for some columns as a starting point for the next `solve`.
	///
	/// A hint, never a constraint: a backend may ignore it, and it stays correct
	/// if the values are infeasible or contradict each other. Sparse because
	/// what a caller knows is usually a prefix of the columns — the variables a
	/// previous run shared with this one — and saying nothing about a column is
	/// not the same as saying zero.
	///
	/// The default does nothing, which is what a backend with no such notion
	/// wants: unlike the capability-gated methods above, nothing declares this,
	/// so it must be safe to leave alone.
	virtual void set_start(std::size_t n, const int* cols, const double* values);

	/// Drop every row from `first` onwards, and every column from `first_col`.
	///
	/// Only a suffix, which is the only shape the layer rules can ask for:
	/// indices follow layer order, so retracting layers retracts a range at the
	/// end and never renumbers anything below it. Reached only when
	/// `Capabilities::incremental` is set.
	virtual void truncate(std::size_t first_row, std::size_t first_col);
};

} // namespace fznso_mip

#endif // FZNSO_MIP_BACKEND_HH

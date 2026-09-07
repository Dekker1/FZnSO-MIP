#include "signatures.hh"

#include <cstring>

namespace fznso_mip {

namespace {

/// Storage the option defaults borrow. A `fznso::Value` points at its payload
/// rather than copying it, so these outlive every use of the list.
struct Defaults {
	bool no = false;
	std::int64_t one = 1;
};

const Defaults& defaults() {
	static const Defaults d;
	return d;
}

} // namespace

void Declarations::declare(const char* ident, std::vector<FznsoType> args) {
	names_.push_back(ident);
	arguments_.push_back(std::move(args));
}

Declarations::Declarations(Capabilities capabilities, std::vector<FznsoOption> extra_options,
                           std::vector<FznsoStatistic> extra_statistics) {
	const bool floats = capabilities.floats;

	// Everything declared here is one row or one column property: no auxiliary
	// variable, no second constraint posted. A disequality, a reified form and
	// a conjunction are all several rows or a fresh binary, so they are library
	// work and are deliberately absent.
	declare("int_lin_eq", {kAI, kAVI, kI});
	declare("int_lin_le", {kAI, kAVI, kI});
	// A Boolean is a binary column, so a weighted count of Booleans is a row
	// like any other. This is what lets a `var bool` stay a `var bool`.
	declare("bool_lin_eq", {kAI, kAVB, kVI});
	declare("bool_lin_le", {kAI, kAVB, kI});
	// A clause is `Σ pos − Σ neg ≥ 1 − |neg|`: one row, nothing introduced.
	declare("bool_clause", {kAVB, kAVB});
	// Both sides of these are the same column, decided before any is created.
	declare("bool_to_int", {kVB, kVI});
	if (floats) {
		declare("float_lin_eq", {kAF, kAVF, kF});
		declare("float_lin_le", {kAF, kAVF, kF});
		declare("int_to_float", {kVI, kVF});
	}
	if (capabilities.indicators) {
		// An indicator constraint is a half-reified linear constraint, so the
		// registry already names it. A backend without them leaves these
		// undeclared and gets the library's big-M decomposition instead.
		declare("int_lin_le_imp", {kAI, kAVI, kI, kVB});
		declare("int_lin_eq_imp", {kAI, kAVI, kI, kVB});
		if (floats) {
			declare("float_lin_le_imp", {kAF, kAVF, kF, kVB});
			declare("float_lin_eq_imp", {kAF, kAVF, kF, kVB});
		}
	}
	if (capabilities.quadratic) {
		declare("int_times", {kVI, kVI, kVI});
		if (floats) {
			declare("float_times", {kVF, kVF, kVF});
		}
	}

	constraints_.reserve(names_.size());
	for (std::size_t i = 0; i < names_.size(); i++) {
		const std::vector<FznsoType>& a = arguments_[i];
		constraints_.push_back(
			FznsoConstraintType{fznso::str(names_[i]), a.size(), a.data()});
	}

	decisions_ = {kVB, kVI};
	if (floats) {
		decisions_.push_back(kVF);
	}

	objectives_ = {
		FznsoObjective{fznso::str("int_minimize"), kVI},
		FznsoObjective{fznso::str("int_maximize"), kVI},
	};
	if (floats) {
		objectives_.push_back(FznsoObjective{fznso::str("float_minimize"), kVF});
		objectives_.push_back(FznsoObjective{fznso::str("float_maximize"), kVF});
	}

	options_ = {
		FznsoOption{fznso::str("intermediate"), kB, fznso::Value{defaults().no}.raw()},
		FznsoOption{fznso::str("threads"), kI, fznso::Value{defaults().one}.raw()},
		FznsoOption{fznso::str("time_limit"), kOptI, fznso::Value{}.raw()},
		FznsoOption{fznso::str("random_seed"), kOptI, fznso::Value{}.raw()},
		FznsoOption{fznso::str("verbose"), kB, fznso::Value{defaults().no}.raw()},
	};
	if (capabilities.solution_pool) {
		// Without a solution pool there is no way to report the solutions that
		// tie with the optimum, so the option is not declared rather than
		// declared and ignored.
		options_.push_back(
			FznsoOption{fznso::str("all_solutions"), kB, fznso::Value{defaults().no}.raw()});
	}
	options_.insert(options_.end(), extra_options.begin(), extra_options.end());

	// `{ident, type, from a solution, from the solver}`. The objective and the
	// counters are readable while a run is in progress; the model sizes and the
	// timings only once it has finished.
	statistics_ = {
		FznsoStatistic{fznso::str("solutions"), kI, true, true},
		FznsoStatistic{fznso::str("nodes"), kI, true, true},
		FznsoStatistic{fznso::str("int_objective"), kI, true, true},
		FznsoStatistic{fznso::str("int_objective_bound"), kI, false, true},
		FznsoStatistic{fznso::str("decisions"), kI, false, true},
		FznsoStatistic{fznso::str("constraints"), kI, false, true},
		FznsoStatistic{fznso::str("init_time"), kF, false, true},
		FznsoStatistic{fznso::str("solve_time"), kF, false, true},
	};
	if (floats) {
		statistics_.push_back(FznsoStatistic{fznso::str("float_objective"), kF, true, true});
		statistics_.push_back(
			FznsoStatistic{fznso::str("float_objective_bound"), kF, false, true});
	}
	statistics_.insert(statistics_.end(), extra_statistics.begin(), extra_statistics.end());
}

} // namespace fznso_mip

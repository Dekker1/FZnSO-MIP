// HiGHS behind the shared MIP core.
//
// Every HiGHS call here has a counterpart in MiniZinc's own
// `solvers/MIP/MIP_highs_wrap.cpp`, which is the reference this backend is
// compared against.

#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <interfaces/highs_c_api.h>

#include "mip_backend.hh"
#include "mip_solver.hh"
#include "signatures.hh"

namespace {

using namespace fznso_mip;

/// HiGHS is a plain linear MIP: no indicator constraints, no quadratic rows and
/// no solution pool. The declared list and the constraint dispatch both read
/// this, so they cannot drift apart.
constexpr Capabilities kHighsCaps{/*indicators=*/false, /*quadratic=*/false,
                                  /*solution_pool=*/false, /*floats=*/true};

/// What the HiGHS callback is given, since it cannot be a member function.
struct CallbackState {
	MipSink* sink = nullptr;
	std::size_t columns = 0;
	std::size_t streamed = 0;
};

class HighsBackend final : public MipBackend {
public:
	HighsBackend() : highs_(Highs_create()) {
		if (highs_ == nullptr) {
			throw std::runtime_error("HiGHS refused to create a solver instance");
		}
		Highs_setBoolOptionValue(highs_, "log_to_console", 0);
	}

	~HighsBackend() override { Highs_destroy(highs_); }

	Capabilities capabilities() const override { return kHighsCaps; }

	double infinity() const override { return Highs_getInfinity(highs_); }

	void reset() override {
		check(Highs_clearModel(highs_), "unable to clear the model");
		check(Highs_clearSolver(highs_), "unable to clear the solver");
	}

	void add_columns(std::size_t n, const double* obj, const double* lb, const double* ub,
	                 const ColKind* kind) override {
		if (n == 0) {
			return;
		}
		HighsInt first = Highs_getNumCol(highs_);
		check(Highs_addCols(highs_, static_cast<HighsInt>(n), obj, lb, ub, 0, nullptr, nullptr,
		                    nullptr),
		      "unable to add columns");
		std::vector<HighsInt> integrality(n);
		for (std::size_t i = 0; i < n; i++) {
			integrality[i] =
				kind[i] == ColKind::Integer ? kHighsVarTypeInteger : kHighsVarTypeContinuous;
		}
		check(Highs_changeColsIntegralityByRange(highs_, first, Highs_getNumCol(highs_) - 1,
		                                         integrality.data()),
		      "unable to set column integrality");
	}

	void add_row(std::size_t nnz, const int* idx, const double* val, RowSense sense,
	             double rhs) override {
		// A sense is a pair of row bounds; the unused side is infinite.
		double inf = Highs_getInfinity(highs_);
		double lower = sense == RowSense::Le ? -inf : rhs;
		double upper = sense == RowSense::Ge ? inf : rhs;
		check(Highs_addRow(highs_, lower, upper, static_cast<HighsInt>(nnz), idx, val),
		      "unable to add a row");
	}

	void set_objective_sense(bool maximise) override {
		check(Highs_changeObjectiveSense(highs_,
		                                 maximise ? kHighsObjSenseMaximize : kHighsObjSenseMinimize),
		      "unable to set the objective sense");
	}

	RunOutcome solve(const MipOptions& options, MipSink& sink) override;

	std::optional<std::string> option_set(std::string_view name, fznso::Value value) override {
		if (name == "highs_write_model") {
			if (value.kind() != FznsoValueString) {
				return "option `highs_write_model' expects a string";
			}
			write_model_ = std::string{value.as_string()};
			return std::nullopt;
		}
		if (name == "highs_abs_gap" || name == "highs_rel_gap" || name == "highs_int_tol") {
			if (value.kind() != FznsoValueFloat) {
				return "option `" + std::string{name} + "' expects a float";
			}
			double v = value.as_float();
			if (v < 0.0) {
				return "option `" + std::string{name} + "' must not be negative, got " +
				       std::to_string(v);
			}
			(name == "highs_abs_gap" ? abs_gap_ : name == "highs_rel_gap" ? rel_gap_ : int_tol_) = v;
			return std::nullopt;
		}
		return "unknown option `" + std::string{name} + "'";
	}

	fznso::Value option_get(std::string_view name) const override {
		if (name == "highs_write_model") {
			return keep(name, fznso::OwnedValue{write_model_});
		}
		if (name == "highs_abs_gap") {
			return keep(name, fznso::OwnedValue{abs_gap_});
		}
		if (name == "highs_rel_gap") {
			return keep(name, fznso::OwnedValue{rel_gap_});
		}
		if (name == "highs_int_tol") {
			return keep(name, fznso::OwnedValue{int_tol_});
		}
		return fznso::Value{};
	}

	fznso::Value statistic(std::string_view name) const override {
		return name == "nodes" ? keep(name, fznso::OwnedValue{nodes_}) : fznso::Value{};
	}

private:
	void check(HighsInt status, const char* what) const {
		// A warning is not a failure; MiniZinc's own wrapper tolerates it too.
		if (status == kHighsStatusError) {
			throw std::runtime_error(std::string{"HiGHS: "} + what);
		}
	}

	/// A borrowed value needs its payload to outlive the call, and an earlier
	/// read must survive a later one — hence a node-based container.
	fznso::Value keep(std::string_view name, fznso::OwnedValue value) const {
		auto it = cache_.insert_or_assign(std::string{name}, std::move(value)).first;
		return fznso::Value{it->second};
	}

	static void callback(int type, const char* message, const HighsCallbackDataOut* data_out,
	                     HighsCallbackDataIn* data_in, void* user_data);

	void* highs_ = nullptr;
	std::string write_model_;
	double abs_gap_ = -1.0;
	double rel_gap_ = 1e-8;
	double int_tol_ = 1e-8;
	std::int64_t nodes_ = 0;
	mutable std::map<std::string, fznso::OwnedValue> cache_;
};

void HighsBackend::callback(int type, const char* message, const HighsCallbackDataOut* data_out,
                            HighsCallbackDataIn* data_in, void* user_data) {
	auto* state = static_cast<CallbackState*>(user_data);
	auto out = [&](const char* item) {
		return static_cast<const double*>(Highs_getCallbackDataOutItem(data_out, item));
	};
	switch (type) {
	case kHighsCallbackLogging:
		if (message != nullptr) {
			state->sink->log(message);
		}
		break;
	case kHighsCallbackMipInterrupt:
		// The only callback HiGHS reads `user_interrupt` from; setting it
		// anywhere else trips an assertion inside `HighsCallback::callbackAction`.
		if (data_in != nullptr) {
			data_in->user_interrupt = state->sink->should_stop() ? 1 : 0;
		}
		break;
	case kHighsCallbackMipImprovingSolution: {
		if (const double* bound = out(kHighsCallbackDataOutMipDualBoundName)) {
			state->sink->bound(*bound);
		}
		if (const double* values = out(kHighsCallbackDataOutMipSolutionName)) {
			const double* objective = out(kHighsCallbackDataOutObjectiveFunctionValueName);
			state->sink->solution(values, state->columns, objective != nullptr ? *objective : 0.0);
			state->streamed++;
		}
		break;
	}
	default:
		break;
	}
}

RunOutcome HighsBackend::solve(const MipOptions& options, MipSink& sink) {
	RunOutcome outcome;

	if (options.threads > 1) {
		check(Highs_setIntOptionValue(highs_, "threads", static_cast<HighsInt>(options.threads)),
		      "unable to set the thread count");
		check(Highs_setStringOptionValue(highs_, "parallel", "on"),
		      "unable to enable parallel mode");
	} else {
		check(Highs_setStringOptionValue(highs_, "parallel", "off"),
		      "unable to disable parallel mode");
	}
	if (options.time_limit_ms.has_value()) {
		check(Highs_setDoubleOptionValue(highs_, "time_limit",
		                                 static_cast<double>(*options.time_limit_ms) / 1000.0),
		      "unable to set the time limit");
	}
	if (options.random_seed.has_value()) {
		check(Highs_setIntOptionValue(highs_, "random_seed",
		                              static_cast<HighsInt>(*options.random_seed)),
		      "unable to set the random seed");
	}
	if (abs_gap_ >= 0.0) {
		check(Highs_setDoubleOptionValue(highs_, "mip_abs_gap", abs_gap_),
		      "unable to set the absolute gap");
	}
	if (rel_gap_ >= 0.0) {
		check(Highs_setDoubleOptionValue(highs_, "mip_rel_gap", rel_gap_),
		      "unable to set the relative gap");
	}
	if (int_tol_ >= 0.0) {
		check(Highs_setDoubleOptionValue(highs_, "mip_feasibility_tolerance", int_tol_),
		      "unable to set the integrality tolerance");
	}
	check(Highs_setBoolOptionValue(highs_, "log_to_console", 0), "unable to set verbosity");
	if (!write_model_.empty()) {
		check(Highs_writeModel(highs_, write_model_.c_str()), "unable to write the model");
	}

	CallbackState state;
	state.sink = &sink;
	state.columns = static_cast<std::size_t>(Highs_getNumCol(highs_));

	const bool want_log = options.verbose && sink.log_wanted();
	check(Highs_setCallback(highs_, &HighsBackend::callback, &state),
	      "unable to install the callback");
	// The interrupt callback runs whether or not anything is polling it: it is
	// also where a `should_stop` mid-search is acted on.
	check(Highs_startCallback(highs_, kHighsCallbackMipInterrupt),
	      "unable to start the interrupt callback");
	if (want_log) {
		check(Highs_startCallback(highs_, kHighsCallbackLogging),
		      "unable to start the logging callback");
	}
	if (options.intermediate) {
		check(Highs_startCallback(highs_, kHighsCallbackMipImprovingSolution),
		      "unable to start the solution callback");
	}

	HighsInt run_status = Highs_run(highs_);

	Highs_stopCallback(highs_, kHighsCallbackMipInterrupt);
	if (want_log) {
		Highs_stopCallback(highs_, kHighsCallbackLogging);
	}
	if (options.intermediate) {
		Highs_stopCallback(highs_, kHighsCallbackMipImprovingSolution);
	}
	outcome.streamed = state.streamed;

	if (run_status == kHighsStatusError) {
		outcome.status = MipStatus::Error;
		outcome.error = "HiGHS: unable to solve the model";
		return outcome;
	}

	std::int64_t nodes = 0;
	if (Highs_getInt64InfoValue(highs_, "mip_node_count", &nodes) != kHighsStatusError) {
		nodes_ = nodes;
	}
	double bound = 0.0;
	if (Highs_getDoubleInfoValue(highs_, "mip_dual_bound", &bound) != kHighsStatusError) {
		sink.bound(bound);
	}

	HighsInt primal = 0;
	Highs_getIntInfoValue(highs_, "primal_solution_status", &primal);
	const bool feasible = primal == kHighsSolutionStatusFeasible;

	// The mapping MiniZinc's own backend uses, in `MIPHiGHSWrapper::convertStatus`.
	switch (Highs_getModelStatus(highs_)) {
	case kHighsModelStatusOptimal:
	// No columns and no rows: the empty assignment is a solution.
	case kHighsModelStatusModelEmpty:
		outcome.status = MipStatus::Optimal;
		break;
	case kHighsModelStatusInfeasible:
		outcome.status = MipStatus::Infeasible;
		break;
	case kHighsModelStatusUnbounded:
	case kHighsModelStatusUnboundedOrInfeasible:
		outcome.status = MipStatus::Unbounded;
		break;
	case kHighsModelStatusObjectiveBound:
	case kHighsModelStatusObjectiveTarget:
		outcome.status = feasible ? MipStatus::Optimal : MipStatus::Infeasible;
		break;
	case kHighsModelStatusTimeLimit:
	case kHighsModelStatusIterationLimit:
	case kHighsModelStatusSolutionLimit:
	case kHighsModelStatusInterrupt:
		outcome.status = feasible ? MipStatus::Feasible : MipStatus::Unknown;
		break;
	case kHighsModelStatusNotset:
	case kHighsModelStatusUnknown:
		outcome.status = MipStatus::Unknown;
		break;
	default:
		outcome.status = MipStatus::Error;
		outcome.error = "HiGHS could not solve the model";
		return outcome;
	}

	if (outcome.status == MipStatus::Optimal || outcome.status == MipStatus::Feasible) {
		outcome.values.resize(state.columns);
		if (state.columns > 0) {
			check(Highs_getSolution(highs_, outcome.values.data(), nullptr, nullptr, nullptr),
			      "unable to read the solution");
		}
		outcome.objective = Highs_getObjectiveValue(highs_);
		outcome.has_solution = true;
	}
	return outcome;
}

/// Storage the option defaults borrow, since an `FznsoOption` points at its
/// default rather than copying it.
struct Defaults {
	double abs_gap = -1.0;
	double rel_gap = 1e-8;
	double int_tol = 1e-8;
	std::string empty;
};

const Defaults& defaults() {
	static const Defaults d;
	return d;
}

const Declarations& declarations() {
	static const Declarations d{
		kHighsCaps,
		std::vector<FznsoOption>{
			{fznso::str("highs_write_model"), kStr, fznso::Value{defaults().empty}.raw()},
			{fznso::str("highs_abs_gap"), kF, fznso::Value{defaults().abs_gap}.raw()},
			{fznso::str("highs_rel_gap"), kF, fznso::Value{defaults().rel_gap}.raw()},
			{fznso::str("highs_int_tol"), kF, fznso::Value{defaults().int_tol}.raw()},
		},
		std::vector<FznsoStatistic>{}};
	return d;
}

class HighsSolver final : public MipSolver {
public:
	HighsSolver() : MipSolver(std::make_unique<HighsBackend>()) {}

	static FznsoConstraintList constraint_list() { return declarations().constraints(); }
	static FznsoTypeList decision_list() { return declarations().decisions(); }
	static FznsoObjectiveList objective_list() { return declarations().objectives(); }
	static FznsoOptionList option_list() { return declarations().options(); }
	static FznsoStatisticList statistic_list() { return declarations().statistics(); }
};

} // namespace

// The library is `libhighs.*`, so the entry points are `fznso_highs_…`.
FZNSO_EXPORT_SOLVER(HighsSolver, highs);

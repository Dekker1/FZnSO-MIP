#include "mip_solver.hh"

#include <chrono>
#include <cmath>
#include <vector>

namespace fznso_mip {

namespace {

/// One solution's assignment, copied out of the column values.
///
/// Everything handed across the interface is borrowed for the duration of the
/// callback, so the values are copied up front rather than read from the
/// backend on demand.
class Solution final : public fznso::SolutionSource {
public:
	Solution(std::vector<fznso::OwnedValue> values, std::int64_t index, bool float_objective,
	         std::optional<double> objective)
		: values_(std::move(values)),
		  index_(index),
		  objective_(objective.has_value() ? (float_objective
		                                          ? fznso::OwnedValue{*objective}
		                                          : fznso::OwnedValue{static_cast<std::int64_t>(
																std::llround(*objective))})
		                                   : fznso::OwnedValue{}),
		  objective_name_(float_objective ? "float_objective" : "int_objective") {}

	fznso::Value value(fznso::Decision decision) const override {
		return decision.index < values_.size() ? fznso::Value{values_[decision.index]}
		                                      : fznso::Value{};
	}

	fznso::Value statistic(std::string_view name) const override {
		if (name == "solutions") {
			return fznso::Value{index_};
		}
		if (name == objective_name_) {
			return fznso::Value{objective_};
		}
		return fznso::Value{};
	}

private:
	std::vector<fznso::OwnedValue> values_;
	std::int64_t index_;
	fznso::OwnedValue objective_;
	std::string_view objective_name_;
};

double seconds_since(std::chrono::steady_clock::time_point start) {
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

fznso::Value MipSolver::cached(const std::string& name, fznso::OwnedValue value) const {
	auto it = cache_.insert_or_assign(name, std::move(value)).first;
	return fznso::Value{it->second};
}

fznso::Value MipSolver::option_get(std::string_view name) const {
	if (name == "intermediate") {
		return cached("intermediate", fznso::OwnedValue{options_.intermediate});
	}
	if (name == "all_solutions") {
		return cached("all_solutions", fznso::OwnedValue{options_.all_solutions});
	}
	if (name == "verbose") {
		return cached("verbose", fznso::OwnedValue{options_.verbose});
	}
	if (name == "threads") {
		return cached("threads", fznso::OwnedValue{options_.threads});
	}
	if (name == "time_limit") {
		return cached("time_limit", fznso::OwnedValue{options_.time_limit_ms});
	}
	if (name == "random_seed") {
		return cached("random_seed", fznso::OwnedValue{options_.random_seed});
	}
	return backend_->option_get(name);
}

std::optional<std::string> MipSolver::option_set(std::string_view name, fznso::Value value) {
	auto reject = [&](const char* want) {
		return "option `" + std::string{name} + "' expects " + want;
	};
	if (name == "intermediate" || name == "all_solutions" || name == "verbose") {
		if (value.kind() != FznsoValueBool) {
			return reject("a boolean");
		}
		bool v = value.as_bool();
		if (name == "intermediate") {
			options_.intermediate = v;
		} else if (name == "all_solutions") {
			options_.all_solutions = v;
		} else {
			options_.verbose = v;
		}
		return std::nullopt;
	}
	if (name == "threads") {
		if (value.kind() != FznsoValueInt) {
			return reject("an integer");
		}
		if (value.as_int() < 1) {
			return "option `threads' must be at least 1, got " + std::to_string(value.as_int());
		}
		options_.threads = value.as_int();
		return std::nullopt;
	}
	if (name == "time_limit") {
		if (value.kind() == FznsoValueAbsent) {
			options_.time_limit_ms.reset();
			return std::nullopt;
		}
		if (value.kind() != FznsoValueInt) {
			return reject("an integer or the absent value");
		}
		// The type carries no range, so a non-positive budget is rejected here.
		if (value.as_int() <= 0) {
			return "option `time_limit' must be positive, got " + std::to_string(value.as_int());
		}
		options_.time_limit_ms = value.as_int();
		return std::nullopt;
	}
	if (name == "random_seed") {
		if (value.kind() == FznsoValueAbsent) {
			options_.random_seed.reset();
			return std::nullopt;
		}
		if (value.kind() != FznsoValueInt) {
			return reject("an integer or the absent value");
		}
		options_.random_seed = value.as_int();
		return std::nullopt;
	}
	return backend_->option_set(name, value);
}

fznso::Value MipSolver::statistic(std::string_view name) const {
	if (name == "solutions") {
		return cached("solutions", fznso::OwnedValue{solutions_});
	}
	if (name == "decisions") {
		return cached("decisions", fznso::OwnedValue{static_cast<std::int64_t>(columns_)});
	}
	if (name == "constraints") {
		return cached("constraints", fznso::OwnedValue{static_cast<std::int64_t>(rows_)});
	}
	if (name == "init_time") {
		return cached("init_time", fznso::OwnedValue{init_time_});
	}
	if (name == "solve_time") {
		return cached("solve_time", fznso::OwnedValue{solve_time_});
	}
	if (have_objective_) {
		if (name == (float_objective_ ? "float_objective" : "int_objective")) {
			return cached(std::string{name},
			              float_objective_ ? fznso::OwnedValue{objective_}
			                               : fznso::OwnedValue{static_cast<std::int64_t>(
											     std::llround(objective_))});
		}
	}
	if (have_bound_) {
		if (name == (float_objective_ ? "float_objective_bound" : "int_objective_bound")) {
			return cached(std::string{name},
			              float_objective_ ? fznso::OwnedValue{bound_}
			                               : fznso::OwnedValue{static_cast<std::int64_t>(
											     std::llround(bound_))});
		}
	}
	return backend_->statistic(name);
}

fznso::Status MipSolver::run(const fznso::Model& model, fznso::SolutionSink& solutions,
                             fznso::MessageSink& messages, const fznso::StopSignal& stop) {
	// Polled before starting, so a caller that has already asked to stop gets no
	// search at all.
	if (stop.requested()) {
		return fznso::Status{fznso::Status::Kind::Incomplete, {}};
	}

	auto started = std::chrono::steady_clock::now();

	// No incrementality: the model is rebuilt every run, so `layer_unchanged()`
	// is ignored. Always correct, just not incremental.
	backend_->reset();
	MipModel built;
	std::string error = built.build(model, *backend_);
	if (!error.empty()) {
		return fznso::Status{fznso::Status::Kind::Error, std::move(error)};
	}
	columns_ = built.column_count();
	rows_ = built.row_count();
	init_time_ = seconds_since(started);

	std::string_view objective = model.objective_ident();
	have_objective_ = !objective.empty();
	float_objective_ = objective == "float_minimize" || objective == "float_maximize";
	have_bound_ = false;

	// Which decisions need a value, resolved once rather than per solution.
	const std::size_t decisions = model.decision_count();
	std::vector<bool> wanted(decisions);
	for (std::size_t d = 0; d < decisions; d++) {
		wanted[d] = model.decision_in_solution(fznso::Decision{d});
	}

	/// Turns column values into decision values and pushes them at the sinks.
	class Sink final : public MipSink {
	public:
		Sink(MipSolver& owner, const MipModel& built, const std::vector<bool>& wanted,
		     fznso::SolutionSink& solutions, fznso::MessageSink& messages,
		     const fznso::StopSignal& stop)
			: owner_(owner),
			  built_(built),
			  wanted_(wanted),
			  solutions_(solutions),
			  messages_(messages),
			  stop_(stop) {}

		void solution(const double* values, std::size_t len, double objective) override {
			std::vector<fznso::OwnedValue> assignment(wanted_.size());
			for (std::size_t d = 0; d < wanted_.size(); d++) {
				if (!wanted_[d]) {
					continue;
				}
				auto col = static_cast<std::size_t>(built_.column_of(d));
				if (col >= len) {
					continue;
				}
				double v = values[col];
				switch (built_.value_kind(d)) {
				case ValueKind::Bool:
					assignment[d] = fznso::OwnedValue{v > 0.5};
					break;
				case ValueKind::Int:
					assignment[d] = fznso::OwnedValue{static_cast<std::int64_t>(std::llround(v))};
					break;
				case ValueKind::Float:
					assignment[d] = fznso::OwnedValue{v};
					break;
				}
			}
			owner_.objective_ = objective;
			Solution src{std::move(assignment), owner_.solutions_, owner_.float_objective_,
			             owner_.have_objective_ ? std::optional<double>{objective}
			                                    : std::nullopt};
			owner_.solutions_++;
			solutions_.solution(src);
			// Polled again here, which is what makes "stop after this solution"
			// reliable.
			stopped_ = stopped_ || stop_.requested();
		}

		void bound(double dual_bound) override {
			owner_.bound_ = dual_bound;
			owner_.have_bound_ = true;
			if (messages_.wanted()) {
				messages_.message("progress.bound", fznso::Value{dual_bound});
			}
		}

		void log(std::string_view text) override {
			if (messages_.wanted()) {
				std::string line{text};
				messages_.message("log", fznso::Value{line});
			}
		}

		bool should_stop() override {
			stopped_ = stopped_ || stop_.requested();
			return stopped_;
		}

		bool log_wanted() const override { return messages_.wanted(); }

		bool stopped() const { return stopped_; }

	private:
		MipSolver& owner_;
		const MipModel& built_;
		const std::vector<bool>& wanted_;
		fznso::SolutionSink& solutions_;
		fznso::MessageSink& messages_;
		const fznso::StopSignal& stop_;
		bool stopped_ = false;
	};

	Sink sink{*this, built, wanted, solutions, messages, stop};

	auto solve_started = std::chrono::steady_clock::now();
	RunOutcome outcome = backend_->solve(options_, sink);
	solve_time_ = seconds_since(solve_started);

	if (outcome.status == MipStatus::Error) {
		return fznso::Status{fznso::Status::Kind::Error, std::move(outcome.error)};
	}

	// A backend that streams improving solutions has already reported the last
	// one; otherwise the final assignment is reported here.
	if (outcome.has_solution && outcome.streamed == 0) {
		sink.solution(outcome.values.data(), outcome.values.size(), outcome.objective);
	}

	if (sink.stopped()) {
		return fznso::Status{fznso::Status::Kind::Incomplete, {}};
	}
	switch (outcome.status) {
	case MipStatus::Optimal:
	case MipStatus::Infeasible:
		// Unsatisfiable is `Complete` with no solution reported.
		return fznso::Status{fznso::Status::Kind::Complete, {}};
	case MipStatus::Unbounded:
		return fznso::Status{fznso::Status::Kind::Error, "the objective is unbounded"};
	default:
		return fznso::Status{fznso::Status::Kind::Incomplete, {}};
	}
}

} // namespace fznso_mip

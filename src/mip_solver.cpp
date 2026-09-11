#include "mip_solver.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
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

namespace {

/// Collect `warm_start(xs, vs)` out of `annotation`, keyed by decision index.
///
/// Only the flat form arrives. MiniZinc also writes `warm_start_array([...])`
/// and puts warm starts inside `seq_search`, but an annotation argument is a
/// *value* and no value kind is an annotation — so a grouped warm start cannot
/// cross this interface at all, and what reaches here is whatever was written
/// directly on the solve item.
///
/// Everything here is advice: a pair that does not line up, or one naming a
/// decision this model does not have, is skipped rather than reported. A warm
/// start cannot make an answer wrong, so a malformed one is not worth a
/// diagnostic.
void collect_warm_start(fznso::AnnotationRef annotation, std::map<std::size_t, double>& into) {
	if (annotation.ident() != "warm_start" || annotation.size() != 2) {
		return;
	}
	fznso::Value xs = annotation[0];
	fznso::Value vs = annotation[1];
	if (xs.kind() != FznsoValueList || vs.kind() != FznsoValueList || xs.size() != vs.size()) {
		return;
	}
	for (std::size_t i = 0; i < xs.size(); i++) {
		fznso::Value x = xs[i];
		fznso::Value v = vs[i];
		if (x.kind() != FznsoValueDecision) {
			continue;
		}
		switch (v.kind()) {
		case FznsoValueBool:
			into[x.as_decision().index] = v.as_bool() ? 1.0 : 0.0;
			break;
		case FznsoValueInt:
			into[x.as_decision().index] = static_cast<double>(v.as_int());
			break;
		case FznsoValueFloat:
			into[x.as_decision().index] = v.as_float();
			break;
		default:
			break;
		}
	}
}

} // namespace

void MipSolver::remember(const double* values, std::size_t len, const MipModel& built,
                         std::size_t decisions) {
	last_.assign(decisions, 0.0);
	last_known_.assign(decisions, false);
	for (std::size_t d = 0; d < decisions; d++) {
		int col = built.column_of(d);
		if (col < 0 || static_cast<std::size_t>(col) >= len) {
			continue;
		}
		last_[d] = values[static_cast<std::size_t>(col)];
		last_known_[d] = true;
	}
}

void MipSolver::offer_start(const fznso::Model& model, const MipModel& built) {
	std::map<std::size_t, double> start;

	// The model's own advice first, so that a value carried over from the last
	// run overwrites it: an annotation is a guess made before anything was
	// solved, and a solution is one the search itself produced.
	for (std::size_t i = 0; i < model.objective_annotation_count(); i++) {
		collect_warm_start(model.objective_annotation(i), start);
	}

	// Layers say how much of the model the solver has already seen, and index
	// order follows layer order, so decisions below the end of the last
	// unchanged layer are the same variables they were last run. Above it they
	// are not, and a value carried up there would be a guess about a different
	// variable.
	std::size_t unchanged = model.layer_unchanged();
	std::size_t prefix = unchanged == 0 ? 0 : model.decision_layer_end(unchanged - 1);
	prefix = std::min(prefix, last_known_.size());
	for (std::size_t d = 0; d < prefix; d++) {
		if (last_known_[d]) {
			start[d] = last_[d];
		}
	}

	if (start.empty()) {
		return;
	}
	std::vector<int> cols;
	std::vector<double> values;
	cols.reserve(start.size());
	values.reserve(start.size());
	for (const auto& entry : start) {
		if (entry.first >= model.decision_count()) {
			continue;
		}
		int col = built.column_of(entry.first);
		if (col < 0 || static_cast<std::size_t>(col) >= built.column_count()) {
			continue;
		}
		cols.push_back(col);
		values.push_back(entry.second);
	}
	if (!cols.empty()) {
		backend_->set_start(cols.size(), cols.data(), values.data());
	}
}

fznso::Status MipSolver::run(const fznso::Model& model, fznso::SolutionSink& solutions,
                             fznso::MessageSink& messages, const fznso::StopSignal& stop) {
	// Polled before starting, so a caller that has already asked to stop gets no
	// search at all.
	if (stop.requested()) {
		return fznso::Status{fznso::Status::Kind::Incomplete, {}};
	}

	auto started = std::chrono::steady_clock::now();

	// How much of what the backend already holds is still this model. A cost is
	// a column property, fixed when the column was handed over, so a changed
	// objective means starting again whatever the layers say.
	std::string_view objective_ident = model.objective_ident();
	fznso::Value objective_value = model.objective_arg();
	// Which decisions the objective names, in order. A list for a lexicographic
	// objective and one entry for a scalar one, so two objectives of the same
	// kind over different decisions — or the same ones ranked differently —
	// compare unequal, which is what sends the build back to the start.
	std::vector<std::size_t> objective_args;
	if (objective_value.kind() == FznsoValueDecision) {
		objective_args.push_back(objective_value.as_decision().index);
	} else if (!objective_ident.empty() && objective_value.kind() == FznsoValueList) {
		for (std::size_t i = 0; i < objective_value.size(); i++) {
			fznso::Value entry = objective_value[i];
			if (entry.kind() == FznsoValueDecision) {
				objective_args.push_back(entry.as_decision().index);
			}
		}
	}
	bool objective_same =
		built_objective_ == objective_ident && built_objective_args_ == objective_args;

	std::size_t keep = std::min(model.layer_unchanged(), built_layers_);
	if (!backend_->capabilities().incremental || !objective_same || keep == 0 ||
	    keep > layer_end_.size()) {
		keep = 0;
	}
	if (keep == 0) {
		backend_->reset();
		built_ = MipModel{};
		layer_end_.clear();
	} else if (keep < built_layers_) {
		// Layers were retracted. Indices follow layer order, so what they took
		// with them is a suffix of the columns and rows, and cutting it leaves
		// everything below numbered as it was.
		const LayerEnd& end = layer_end_[keep - 1];
		backend_->truncate(end.rows, end.columns);
		built_.truncate(end.decisions, end.columns, end.rows);
		layer_end_.resize(keep);
	}

	std::string error;
	std::size_t layers = model.layer_count();
	for (std::size_t l = keep; l < layers; l++) {
		std::size_t first_decision = l == 0 ? 0 : model.decision_layer_end(l - 1);
		std::size_t first_constraint = l == 0 ? 0 : model.constraint_layer_end(l - 1);
		error = built_.build(model, *backend_, first_decision, first_constraint, l != 0 || keep != 0);
		if (error == MipModel::kNeedsFullBuild) {
			// A layer joined two classes that already had a column apiece, or
			// narrowed one the backend was given earlier. Rare, and starting
			// over is always right.
			backend_->reset();
			built_ = MipModel{};
			layer_end_.clear();
			error = built_.build(model, *backend_);
			break;
		}
		if (!error.empty()) {
			break;
		}
		layer_end_.push_back(
			LayerEnd{built_.column_count(), built_.row_count(), model.decision_layer_end(l)});
	}
	if (!error.empty()) {
		backend_->reset();
		built_ = MipModel{};
		layer_end_.clear();
		built_layers_ = 0;
		return fznso::Status{fznso::Status::Kind::Error, std::move(error)};
	}
	built_layers_ = layers;
	built_objective_ = std::string{objective_ident};
	built_objective_args_ = objective_args;
	if (layer_end_.size() != layers) {
		// The fallback above rebuilt everything in one go, so there are no
		// per-layer ends to truncate to next time.
		layer_end_.clear();
		built_layers_ = 0;
	}
	const MipModel& built = built_;
	columns_ = built.column_count();
	rows_ = built.row_count();
	offer_start(model, built);
	init_time_ = seconds_since(started);

	std::string_view objective = model.objective_ident();
	have_objective_ = !objective.empty();
	float_objective_ =
		objective == "float_minimize" || objective == "float_maximize" ||
		objective == "float_lex_minimize" || objective == "float_lex_maximize";
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
			owner_.remember(values, len, built_, wanted_.size());
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

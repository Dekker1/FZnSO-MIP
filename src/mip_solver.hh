// The FZnSO solver every MIP backend sits behind.
//
// A backend library subclasses this, hands it a `MipBackend`, and forwards the
// five static capability lists to its own `Declarations`.

#ifndef FZNSO_MIP_SOLVER_HH
#define FZNSO_MIP_SOLVER_HH

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fznso_export.hpp"
#include "mip_backend.hh"
#include "mip_model.hh"

namespace fznso_mip {

class MipSolver : public fznso::Solver {
public:
	explicit MipSolver(std::unique_ptr<MipBackend> backend) : backend_(std::move(backend)) {}

	fznso::Value option_get(std::string_view name) const override;
	std::optional<std::string> option_set(std::string_view name, fznso::Value value) override;
	fznso::Value statistic(std::string_view name) const override;
	fznso::Status run(const fznso::Model& model, fznso::SolutionSink& solutions,
	                  fznso::MessageSink& messages, const fznso::StopSignal& stop) override;

protected:
	MipBackend& backend() { return *backend_; }

private:
	/// Hand out a borrowed value, keeping the payload alive in a node-based
	/// container so an earlier read survives a later one.
	fznso::Value cached(const std::string& name, fznso::OwnedValue value) const;

	/// Offer the backend a starting point for this run: whatever the last run
	/// ended with, over the decisions this model still shares with it, and the
	/// model's own `warm_start` annotations for the rest.
	void offer_start(const fznso::Model& model, const MipModel& built);

	/// Remember a solution's column values against their decisions, so that the
	/// next run can start from them.
	void remember(const double* values, std::size_t len, const MipModel& built,
	              std::size_t decisions);

	std::unique_ptr<MipBackend> backend_;
	MipOptions options_;
	mutable std::map<std::string, fznso::OwnedValue> cache_;

	// The last assignment reported, by decision index, and which of those
	// indices it actually covers. Kept across runs: an instance is created,
	// configured and run any number of times, so the values a run ends with are
	// the best guess the next one has.
	std::vector<double> last_;
	std::vector<bool> last_known_;

	// The model as it stands in the backend, and how much of it each layer
	// accounts for. Kept so that the next run can truncate to a layer boundary
	// and post only what lies above it, which is what `layer_unchanged()` is
	// for. `built_layers_` is zero when the backend holds nothing.
	MipModel built_;
	std::size_t built_layers_ = 0;
	struct LayerEnd {
		std::size_t columns;
		std::size_t rows;
		std::size_t decisions;
	};
	std::vector<LayerEnd> layer_end_;
	std::string built_objective_;
	std::size_t built_objective_arg_ = 0;
	bool built_objective_is_decision_ = false;

	// Statistics of the last run.
	std::int64_t solutions_ = 0;
	double init_time_ = 0.0;
	double solve_time_ = 0.0;
	std::size_t columns_ = 0;
	std::size_t rows_ = 0;
	bool have_objective_ = false;
	bool float_objective_ = false;
	double objective_ = 0.0;
	double bound_ = 0.0;
	bool have_bound_ = false;
};

} // namespace fznso_mip

#endif // FZNSO_MIP_SOLVER_HH

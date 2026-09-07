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

	std::unique_ptr<MipBackend> backend_;
	MipOptions options_;
	mutable std::map<std::string, fznso::OwnedValue> cache_;

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

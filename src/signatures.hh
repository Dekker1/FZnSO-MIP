// The five capability lists, derived from what a backend says it can do.
//
// A backend holds one `Declarations` as a function-local `static` and forwards
// the five `fznso::Solver` list functions to it.

#ifndef FZNSO_MIP_SIGNATURES_HH
#define FZNSO_MIP_SIGNATURES_HH

#include <vector>

#include "fznso.hpp"
#include "mip_backend.hh"

namespace fznso_mip {

/// Argument type shorthands. `V` marks the decision-variable form, `A` the list.
constexpr FznsoType kB = fznso::Type{FznsoTypeBaseBool};
constexpr FznsoType kVB = fznso::Type{FznsoTypeBaseBool}.decision(true);
constexpr FznsoType kI = fznso::Type{FznsoTypeBaseInt};
constexpr FznsoType kVI = fznso::Type{FznsoTypeBaseInt}.decision(true);
constexpr FznsoType kF = fznso::Type{FznsoTypeBaseFloat};
constexpr FznsoType kVF = fznso::Type{FznsoTypeBaseFloat}.decision(true);
constexpr FznsoType kStr = fznso::Type{FznsoTypeBaseString};
constexpr FznsoType kOptI = fznso::Type{FznsoTypeBaseInt}.opt(true);
constexpr FznsoType kAI = fznso::Type{FznsoTypeBaseInt}.list(true);
constexpr FznsoType kAVI = fznso::Type{FznsoTypeBaseInt}.list(true).decision(true);
constexpr FznsoType kAF = fznso::Type{FznsoTypeBaseFloat}.list(true);
constexpr FznsoType kAVF = fznso::Type{FznsoTypeBaseFloat}.list(true).decision(true);
constexpr FznsoType kAVB = fznso::Type{FznsoTypeBaseBool}.list(true).decision(true);

/// What one backend declares.
///
/// Built once per library. The `FznsoConstraintType`s point into this object's
/// own storage, so it must outlive every use of the lists — which, being a
/// `static`, it does.
class Declarations {
public:
	/// `extra_options` and `extra_statistics` are the backend's own
	/// `<solver>_`-prefixed entries; their identifiers and default values must
	/// point at storage that outlives this object.
	Declarations(Capabilities capabilities, std::vector<FznsoOption> extra_options,
	             std::vector<FznsoStatistic> extra_statistics);

	FznsoConstraintList constraints() const { return {constraints_.size(), constraints_.data()}; }
	FznsoTypeList decisions() const { return {decisions_.size(), decisions_.data()}; }
	FznsoObjectiveList objectives() const { return {objectives_.size(), objectives_.data()}; }
	FznsoOptionList options() const { return {options_.size(), options_.data()}; }
	FznsoStatisticList statistics() const { return {statistics_.size(), statistics_.data()}; }

private:
	void declare(const char* ident, std::vector<FznsoType> args);

	// A deque would do; a two-phase build over vectors is enough, because the
	// argument arrays are filled first and only then pointed at.
	std::vector<const char*> names_;
	std::vector<std::vector<FznsoType>> arguments_;
	std::vector<FznsoConstraintType> constraints_;
	std::vector<FznsoType> decisions_;
	std::vector<FznsoObjective> objectives_;
	std::vector<FznsoOption> options_;
	std::vector<FznsoStatistic> statistics_;
};

} // namespace fznso_mip

#endif // FZNSO_MIP_SIGNATURES_HH

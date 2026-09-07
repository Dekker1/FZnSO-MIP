// Turning an `fznso::Model` into columns and rows.
//
// Nothing here is specific to one MIP solver: it reads the model through the
// interface and pushes the result at a `MipBackend`.

#ifndef FZNSO_MIP_MODEL_HH
#define FZNSO_MIP_MODEL_HH

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fznso.hpp"
#include "mip_backend.hh"

namespace fznso_mip {

/// How a column's value is read back for the decision it stands for.
enum class ValueKind : std::uint8_t { Bool, Int, Float };

/// The model, built into a backend.
///
/// One instance per run. `build` posts everything and leaves behind the map a
/// solution needs to read column values back out as decision values.
class MipModel {
public:
	/// An inclusive interval a column's value may fall in.
	struct Interval {
		double min;
		double max;
	};

	/// Post `model` into `backend`. Returns an error message, or empty on success.
	std::string build(const fznso::Model& model, MipBackend& backend);

	/// The column standing for a decision.
	int column_of(std::size_t decision) const { return column_[decision]; }
	/// How that decision reads its column's value.
	ValueKind value_kind(std::size_t decision) const { return kind_[decision]; }
	std::size_t decision_count() const { return column_.size(); }
	std::size_t column_count() const { return columns_; }
	std::size_t row_count() const { return rows_; }

private:
	// --- the aliasing pre-pass ---------------------------------------------

	/// Union-find over decisions, so that two joined by a functional identity
	/// share one column.
	std::size_t find(std::size_t d);
	void unite(std::size_t a, std::size_t b);

	// --- row building -------------------------------------------------------

	/// Start a fresh row.
	void row_begin();
	/// Add `coefficient · column`, folding a repeat of the same column into the
	/// term already there.
	void row_add(int column, double coefficient);
	/// Post the row and clear it.
	void row_post(MipBackend& backend, RowSense sense, double rhs);

	/// Read a weighted sum `Σ coeffs[i]·xs[i]` into the current row, returning
	/// what the literal terms contribute — which the caller moves to the
	/// right-hand side.
	double read_linear(const fznso::Value& coeffs, const fznso::Value& xs);

	/// The column an argument names, or -1 when it is a literal (whose value is
	/// written to `literal`).
	int column_arg(const fznso::Value& v, double& literal) const;



	std::vector<std::size_t> parent_;
	/// Per column, the intervals its value may fall in. One interval is the
	/// ordinary case and costs nothing; more than one is a domain with holes,
	/// which `encode_holes` turns into rows.
	std::vector<std::vector<Interval>> intervals_;
	std::vector<int> column_;
	std::vector<ValueKind> kind_;
	std::vector<std::size_t> handled_;

	// Row scratch, reused across every row: `slot_` says where a column's term
	// already sits in the current row, `stamp_` says whether that answer belongs
	// to this row or a previous one, so neither ever has to be cleared.
	std::vector<int> slot_;
	std::vector<std::uint32_t> stamp_;
	std::uint32_t row_id_ = 0;
	std::vector<int> idx_;
	std::vector<double> val_;

	std::size_t columns_ = 0;
	std::size_t rows_ = 0;
};

} // namespace fznso_mip

#endif // FZNSO_MIP_MODEL_HH

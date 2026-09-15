#include "mip_model.hh"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fznso_mip {

namespace {

using Interval = MipModel::Interval;

/// The intervals a domain allows, in order and non-overlapping.
///
/// A set value is already a range list, which is exactly this shape. An absent
/// domain is one interval — the caller passes what the *type* implies, since a
/// `var bool` with no domain is still `[0, 1]`.
///
/// `size()` is read only once the kind says it is a set: an absent value need
/// not answer `len` at all, and the Rust binding's does not.
std::vector<Interval> intervals_of(const fznso::Value& domain, Interval fallback) {
	std::vector<Interval> out;
	switch (domain.kind()) {
	case FznsoValueSetInt: {
		std::size_t n = domain.size();
		out.reserve(n);
		for (std::size_t i = 0; i < n; i++) {
			fznso::Range<std::int64_t> r = domain.int_range(i);
			out.push_back(Interval{static_cast<double>(r.min), static_cast<double>(r.max)});
		}
		break;
	}
	case FznsoValueSetFloat: {
		std::size_t n = domain.size();
		out.reserve(n);
		for (std::size_t i = 0; i < n; i++) {
			fznso::Range<double> r = domain.float_range(i);
			out.push_back(Interval{r.min, r.max});
		}
		break;
	}
	default:
		out.push_back(fallback);
		break;
	}
	if (out.empty()) {
		// An explicitly empty domain. Said as bounds the backend will find
		// infeasible, rather than as a case every caller has to handle.
		out.push_back(Interval{1.0, 0.0});
	}
	return out;
}

/// The intervals in both lists: what two decisions sharing a column may take.
std::vector<Interval> intersect(const std::vector<Interval>& a, const std::vector<Interval>& b) {
	std::vector<Interval> out;
	std::size_t i = 0;
	std::size_t j = 0;
	while (i < a.size() && j < b.size()) {
		double lo = std::max(a[i].min, b[j].min);
		double hi = std::min(a[i].max, b[j].max);
		if (lo <= hi) {
			out.push_back(Interval{lo, hi});
		}
		if (a[i].max < b[j].max) {
			i++;
		} else {
			j++;
		}
	}
	if (out.empty()) {
		out.push_back(Interval{1.0, 0.0});
	}
	return out;
}

/// The identifier a constraint dispatches on.
///
/// A declared constraint enables both the global that lowers to it and the
/// `fzn_`-prefixed predicate in `fznso_constraints/`, and MiniZinc emits
/// whichever name the call site used — so a body in a solver's own library that
/// names `fzn_bool_lin_eq` explicitly, as it must to avoid a rewrite loop,
/// arrives under that name. The prefix records where the call came from rather
/// than what the constraint is.
std::string_view dispatch_ident(std::string_view ident) {
	constexpr std::string_view kPrefix = "fzn_";
	return ident.compare(0, kPrefix.size(), kPrefix) == 0 ? ident.substr(kPrefix.size()) : ident;
}

} // namespace

std::size_t MipModel::find(std::size_t d) {
	while (parent_[d] != d) {
		parent_[d] = parent_[parent_[d]]; // halve the path as we go
		d = parent_[d];
	}
	return d;
}

void MipModel::unite(std::size_t a, std::size_t b) {
	std::size_t ra = find(a);
	std::size_t rb = find(b);
	if (ra != rb) {
		// Point at the lower index, so a root is always the first decision of
		// its class and the column order follows decision order.
		if (ra < rb) {
			parent_[rb] = ra;
		} else {
			parent_[ra] = rb;
		}
	}
}

void MipModel::row_begin() {
	++row_id_;
	idx_.clear();
	val_.clear();
}

void MipModel::row_add(int column, double coefficient) {
	auto c = static_cast<std::size_t>(column);
	if (stamp_[c] == row_id_) {
		val_[static_cast<std::size_t>(slot_[c])] += coefficient;
		return;
	}
	stamp_[c] = row_id_;
	slot_[c] = static_cast<int>(idx_.size());
	idx_.push_back(column);
	val_.push_back(coefficient);
}

void MipModel::row_post(MipBackend& backend, RowSense sense, double rhs) {
	if (idx_.empty()) {
		// Every term folded away, so the row is a statement about constants.
		// Posting it would leave the backend an empty row to presolve; deciding
		// it here costs nothing and an infeasible one still has to be said.
		bool holds = sense == RowSense::Le ? 0.0 <= rhs
		           : sense == RowSense::Ge ? 0.0 >= rhs
		                                   : rhs == 0.0;
		if (holds) {
			return;
		}
		// `0 <= -1`: infeasible, and the only way to say so is a row that is.
	}
	backend.add_row(idx_.size(), idx_.data(), val_.data(), sense, rhs);
	++rows_;
}

int MipModel::column_arg(const fznso::Value& v, double& literal) const {
	switch (v.kind()) {
	case FznsoValueDecision:
		literal = 0.0;
		return column_[v.as_decision().index];
	case FznsoValueBool:
		literal = v.as_bool() ? 1.0 : 0.0;
		return -1;
	case FznsoValueInt:
		literal = static_cast<double>(v.as_int());
		return -1;
	case FznsoValueFloat:
		literal = v.as_float();
		return -1;
	default:
		literal = 0.0;
		return -1;
	}
}

double MipModel::read_linear(const fznso::Value& coeffs, const fznso::Value& xs) {
	double constant = 0.0;
	std::size_t n = xs.size();
	for (std::size_t i = 0; i < n; i++) {
		fznso::Value c = coeffs[i];
		double a = c.kind() == FznsoValueFloat ? c.as_float() : static_cast<double>(c.as_int());
		double literal = 0.0;
		int col = column_arg(xs[i], literal);
		if (col < 0) {
			constant += a * literal;
		} else {
			row_add(col, a);
		}
	}
	return constant;
}

const char* const MipModel::kNeedsFullBuild = "cannot extend this model in place";

void MipModel::truncate(std::size_t decisions, std::size_t columns, std::size_t rows) {
	column_.resize(decisions);
	kind_.resize(decisions);
	parent_.resize(decisions);
	intervals_.resize(columns);
	columns_ = columns;
	rows_ = rows;
}

std::string MipModel::build(const fznso::Model& model, MipBackend& backend,
                            std::size_t first_decision, std::size_t first_constraint,
                            bool extending) {
	const double inf = backend.infinity();
	const std::size_t decisions = model.decision_count();
	const std::size_t constraints = model.constraint_count();

	// Where this call's columns and rows start. On a fresh build both are zero
	// and everything below reads as it always did.
	const std::size_t base_column = extending ? columns_ : 0;

	if (extending) {
		if (first_decision > decisions || first_constraint > constraints ||
		    first_decision > column_.size()) {
			return kNeedsFullBuild;
		}
		parent_.resize(decisions);
		for (std::size_t i = first_decision; i < decisions; i++) {
			parent_[i] = i;
		}
		column_.resize(decisions, -1);
		kind_.resize(decisions, ValueKind::Int);
		handled_.assign(constraints, 0);
		for (std::size_t c = 0; c < first_constraint; c++) {
			handled_[c] = 1; // already posted, or already folded away
		}
	} else {
		parent_.resize(decisions);
		for (std::size_t i = 0; i < decisions; i++) {
			parent_[i] = i;
		}
		column_.assign(decisions, -1);
		kind_.assign(decisions, ValueKind::Int);
		handled_.assign(constraints, 0);
		intervals_.clear();
		rows_ = 0;
		columns_ = 0;
	}

	// --- pass 1: which decisions are the same column ------------------------
	//
	// `bool_to_int` and `int_to_float` are total functions between two
	// decisions, so in a MIP they are one column rather than a column and an
	// equality row. Joining them here is what lets a `var bool` be the binary
	// column a reified constraint's big-M term multiplies, instead of a Boolean
	// channelled into a second column for presolve to remove again.
	for (std::size_t c = first_constraint; c < constraints; c++) {
		std::string_view ident = dispatch_ident(model.constraint_ident(fznso::Constraint{c}));
		if (ident != "bool_to_int" && ident != "int_to_float") {
			continue;
		}
		if (model.constraint_argument_count(fznso::Constraint{c}) != 2) {
			return std::string{ident} + ": expected 2 arguments";
		}
		fznso::Value a = model.constraint_argument(fznso::Constraint{c}, 0);
		fznso::Value b = model.constraint_argument(fznso::Constraint{c}, 1);
		if (a.kind() == FznsoValueDecision && b.kind() == FznsoValueDecision) {
			std::size_t x = a.as_decision().index;
			std::size_t y = b.as_decision().index;
			// Joining two classes that already own a column apiece would mean
			// merging two columns the backend has already been given, which
			// nothing here can do. Rare, and starting over is always right.
			if (extending && column_[find(x)] >= 0 && column_[find(y)] >= 0 &&
			    find(x) != find(y)) {
				return kNeedsFullBuild;
			}
			unite(x, y);
			handled_[c] = 1;
		}
		// With a literal on either side there is nothing to join; pass 2 posts
		// the equality row instead.
	}

	// --- columns ------------------------------------------------------------
	std::vector<double> obj;
	std::vector<double> lb;
	std::vector<double> ub;
	std::vector<ColKind> kinds;
	obj.reserve(decisions);
	lb.reserve(decisions);
	ub.reserve(decisions);
	kinds.reserve(decisions);

	for (std::size_t d = first_decision; d < decisions; d++) {
		FznsoType type = model.decision_type(fznso::Decision{d});
		if (type.set_of || type.list_of) {
			return "decision " + std::to_string(d) + ": this solver has no set or list variables";
		}
		switch (type.base) {
		case FznsoTypeBaseBool:
			kind_[d] = ValueKind::Bool;
			break;
		case FznsoTypeBaseInt:
			kind_[d] = ValueKind::Int;
			break;
		case FznsoTypeBaseFloat:
			kind_[d] = ValueKind::Float;
			break;
		default:
			return "decision " + std::to_string(d) + ": unsupported type";
		}

		// A Boolean is a binary column whatever its domain says; anything else
		// takes its intervals from the domain, and an absent domain means
		// unbounded.
		std::vector<Interval> own =
			kind_[d] == ValueKind::Bool
				? intersect({Interval{0.0, 1.0}},
			                intervals_of(model.decision_domain(fznso::Decision{d}),
			                             Interval{0.0, 1.0}))
				: intervals_of(model.decision_domain(fznso::Decision{d}),
			                   Interval{-inf, inf});

		std::size_t root = find(d);
		if (column_[root] < 0) {
			column_[root] = static_cast<int>(base_column + obj.size());
			obj.push_back(0.0);
			lb.push_back(own.front().min);
			ub.push_back(own.back().max);
			intervals_.push_back(std::move(own));
			// A class is integral if any member is: an `int_to_float` pair is
			// the integer's value seen as a float, not the other way round.
			kinds.push_back(kind_[d] == ValueKind::Float ? ColKind::Continuous : ColKind::Integer);
		} else {
			auto col = static_cast<std::size_t>(column_[root]);
			// A column handed over on an earlier run cannot have its bounds
			// narrowed now — the backend was given them once.
			if (col < base_column) {
				return kNeedsFullBuild;
			}
			intervals_[col] = intersect(intervals_[col], own);
			lb[col - base_column] = intervals_[col].front().min;
			ub[col - base_column] = intervals_[col].back().max;
			if (kind_[d] != ValueKind::Float) {
				kinds[col - base_column] = ColKind::Integer;
			}
		}
		column_[d] = column_[root];
	}

	// --- the objective ------------------------------------------------------
	//
	// Set before the columns are handed over, since a cost is a column property
	// — which is also why extending cannot change it: the column carrying the
	// cost was handed over on an earlier run. The caller only extends when the
	// objective is the one it built for.
	std::string_view objective = model.objective_ident();
	bool maximise = false;
	// A lexicographic objective is not a cost on a column, so it is collected
	// here and handed over once the columns exist.
	bool lexicographic = false;
	std::vector<int> lex_cols;
	if (!objective.empty()) {
		if (objective == "int_minimize" || objective == "float_minimize") {
			maximise = false;
		} else if (objective == "int_maximize" || objective == "float_maximize") {
			maximise = true;
		} else if (objective == "int_lex_minimize" || objective == "float_lex_minimize") {
			maximise = false;
			lexicographic = true;
		} else if (objective == "int_lex_maximize" || objective == "float_lex_maximize") {
			maximise = true;
			lexicographic = true;
		} else {
			return "unsupported objective `" + std::string{objective} + "'";
		}
		fznso::Value arg = model.objective_arg();
		if (lexicographic) {
			// A fixed entry contributes nothing to the ranking — every
			// assignment scores the same on it — so it is dropped rather than
			// given a column of its own.
			std::size_t n = arg.kind() == FznsoValueList ? arg.size() : 0;
			lex_cols.reserve(n);
			for (std::size_t i = 0; i < n; i++) {
				fznso::Value entry = arg[i];
				if (entry.kind() == FznsoValueDecision) {
					lex_cols.push_back(column_[entry.as_decision().index]);
				}
			}
		} else if (arg.kind() == FznsoValueDecision) {
			auto col = static_cast<std::size_t>(column_[arg.as_decision().index]);
			if (col < base_column) {
				if (!extending) {
					obj[col] = 1.0;
				}
				// else: the cost is already on that column from the run that
				// created it.
			} else {
				obj[col - base_column] = 1.0;
			}
		}
		// A fixed objective leaves every cost at zero, which is the same problem.
	}

	// --- domains with holes -------------------------------------------------
	//
	// A column is an interval, so a domain of several intervals needs saying in
	// rows. One binary per interval, exactly one of which holds, and the column
	// held between the interval it selects:
	//
	//     sum(z) = 1,   x >= sum(min_r * z_r),   x <= sum(max_r * z_r)
	//
	// Three rows and one binary per interval — not per *value*, which is what
	// an equality encoding would cost on a wide domain with one hole.
	const std::size_t model_columns = base_column + obj.size();
	std::vector<std::size_t> holey;
	for (std::size_t c = base_column; c < model_columns; c++) {
		if (intervals_[c].size() > 1) {
			holey.push_back(c);
			for (std::size_t r = 0; r < intervals_[c].size(); r++) {
				obj.push_back(0.0);
				lb.push_back(0.0);
				ub.push_back(1.0);
				kinds.push_back(ColKind::Integer);
			}
		}
	}

	columns_ = base_column + obj.size();
	backend.add_columns(obj.size(), obj.data(), lb.data(), ub.data(), kinds.data());
	if (!extending) {
		if (lexicographic) {
			backend.set_lex_objective(lex_cols.size(), lex_cols.data(), maximise);
		} else {
			backend.set_objective_sense(maximise);
		}
	}

	// Scratch, sized to the whole model rather than to this call's share: a new
	// row may name any column, old or new.
	slot_.assign(columns_, 0);
	stamp_.assign(columns_, 0);
	row_id_ = 0;

	{
		auto selector = static_cast<int>(model_columns);
		for (std::size_t c : holey) {
			const std::vector<Interval>& parts = intervals_[c];
			auto n = static_cast<int>(parts.size());
			row_begin();
			for (int r = 0; r < n; r++) {
				row_add(selector + r, 1.0);
			}
			row_post(backend, RowSense::Eq, 1.0);

			row_begin();
			row_add(static_cast<int>(c), 1.0);
			for (int r = 0; r < n; r++) {
				row_add(selector + r, -parts[static_cast<std::size_t>(r)].min);
			}
			row_post(backend, RowSense::Ge, 0.0);

			row_begin();
			row_add(static_cast<int>(c), 1.0);
			for (int r = 0; r < n; r++) {
				row_add(selector + r, -parts[static_cast<std::size_t>(r)].max);
			}
			row_post(backend, RowSense::Le, 0.0);

			selector += n;
		}
	}

	const Capabilities caps = backend.capabilities();

	// --- pass 2: rows -------------------------------------------------------
	for (std::size_t c = first_constraint; c < constraints; c++) {
		if (handled_[c] != 0) {
			continue;
		}
		fznso::Constraint con{c};
		std::string_view ident = dispatch_ident(model.constraint_ident(con));
		std::size_t argc = model.constraint_argument_count(con);

		auto arg = [&](std::size_t i) { return model.constraint_argument(con, i); };
		auto wrong_arity = [&](std::size_t want) {
			return std::string{ident} + ": expected " + std::to_string(want) + " arguments, got " +
			       std::to_string(argc);
		};

		// `Σ coeffs·xs <sense> rhs`, shared by every linear form.
		auto linear = [&](RowSense sense, std::size_t rhs_index) -> std::string {
			row_begin();
			double constant = read_linear(arg(0), arg(1));
			fznso::Value r = arg(rhs_index);
			double rhs = 0.0;
			int col = column_arg(r, rhs);
			if (col >= 0) {
				// A variable right-hand side moves to the left.
				row_add(col, -1.0);
				rhs = 0.0;
			}
			row_post(backend, sense, rhs - constant);
			return {};
		};

		if (ident == "int_lin_le" || ident == "float_lin_le" || ident == "bool_lin_le") {
			if (argc != 3) {
				return wrong_arity(3);
			}
			std::string err = linear(RowSense::Le, 2);
			if (!err.empty()) {
				return err;
			}
		} else if (ident == "int_lin_eq" || ident == "float_lin_eq" || ident == "bool_lin_eq") {
			if (argc != 3) {
				return wrong_arity(3);
			}
			std::string err = linear(RowSense::Eq, 2);
			if (!err.empty()) {
				return err;
			}
		} else if (ident == "bool_clause") {
			if (argc != 2) {
				return wrong_arity(2);
			}
			// `Σ pos + Σ (1 − neg) ≥ 1`, with each negative literal's constant 1
			// moved to the right-hand side.
			row_begin();
			double rhs = 1.0;
			bool satisfied = false;
			fznso::Value pos = arg(0);
			fznso::Value neg = arg(1);
			for (std::size_t i = 0; i < pos.size() && !satisfied; i++) {
				double literal = 0.0;
				int col = column_arg(pos[i], literal);
				if (col >= 0) {
					row_add(col, 1.0);
				} else if (literal != 0.0) {
					satisfied = true;
				}
			}
			for (std::size_t i = 0; i < neg.size() && !satisfied; i++) {
				double literal = 0.0;
				int col = column_arg(neg[i], literal);
				if (col >= 0) {
					row_add(col, -1.0);
					rhs -= 1.0;
				} else if (literal == 0.0) {
					satisfied = true;
				}
			}
			if (!satisfied) {
				row_post(backend, RowSense::Ge, rhs);
			}
		} else if (ident == "bool_to_int" || ident == "int_to_float") {
			// Pass 1 took the two-decision case; what is left has a literal on
			// one side, so it is an ordinary equality.
			if (argc != 2) {
				return wrong_arity(2);
			}
			row_begin();
			double rhs = 0.0;
			double la = 0.0;
			double lb2 = 0.0;
			int ca = column_arg(arg(0), la);
			int cb = column_arg(arg(1), lb2);
			if (ca >= 0) {
				row_add(ca, 1.0);
			} else {
				rhs -= la;
			}
			if (cb >= 0) {
				row_add(cb, -1.0);
			} else {
				rhs += lb2;
			}
			row_post(backend, RowSense::Eq, rhs);
		} else if (caps.indicators &&
		           (ident == "int_lin_le_imp" || ident == "float_lin_le_imp" ||
		            ident == "int_lin_eq_imp" || ident == "float_lin_eq_imp")) {
			if (argc != 4) {
				return wrong_arity(4);
			}
			RowSense sense = ident == "int_lin_le_imp" || ident == "float_lin_le_imp"
			                     ? RowSense::Le
			                     : RowSense::Eq;
			double indicator = 0.0;
			int bin = column_arg(arg(3), indicator);
			if (bin < 0 && indicator == 0.0) {
				continue; // implied by nothing: no constraint at all
			}
			row_begin();
			double constant = read_linear(arg(0), arg(1));
			double rhs = 0.0;
			(void)column_arg(arg(2), rhs);
			rhs -= constant;
			if (bin < 0) {
				row_post(backend, sense, rhs);
			} else {
				backend.add_indicator_row(bin, true, idx_.size(), idx_.data(), val_.data(), sense,
				                          rhs);
				++rows_;
			}
		} else if (caps.quadratic && (ident == "int_times" || ident == "float_times")) {
			if (argc != 3) {
				return wrong_arity(3);
			}
			double la = 0.0;
			double lb2 = 0.0;
			double lc = 0.0;
			int ca = column_arg(arg(0), la);
			int cb = column_arg(arg(1), lb2);
			int cc = column_arg(arg(2), lc);
			if (ca >= 0 && cb >= 0) {
				// Both operands are decisions, so this is genuinely quadratic —
				// whether or not the result is one. A fixed result is the
				// product against a value rather than against a column, which
				// is a quadratic row all the same and not a linearisable one.
				backend.add_quadratic_row(cc, ca, cb, cc >= 0 ? 0.0 : lc);
				++rows_;
			} else {
				// With an operand fixed the product is linear, so say it that way.
				row_begin();
				double rhs = 0.0;
				if (ca < 0 && cb >= 0) {
					row_add(cb, la);
				} else if (cb < 0 && ca >= 0) {
					row_add(ca, lb2);
				} else {
					rhs -= la * lb2;
				}
				if (cc >= 0) {
					row_add(cc, -1.0);
				} else {
					rhs += lc;
				}
				row_post(backend, RowSense::Eq, rhs);
			}
		} else {
			return "unknown constraint `" + std::string{ident} + "'";
		}
	}

	return {};
}

} // namespace fznso_mip

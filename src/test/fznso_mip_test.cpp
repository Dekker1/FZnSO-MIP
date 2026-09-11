// End-to-end checks for a MIP backend behind the FZnSO protocol.
//
// The solver is loaded the way a real consumer loads it — through
// `fznso::Library`, from the built shared object — so the entry-point naming and
// the export path are exercised along with the translation itself.
//
// Usage: fznso-mip-test <path to libhighs.dylib|.so|.dll>

#include "fznso.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

using fznso::Decision;
using fznso::LayeredModel;
using fznso::OwnedValue;
using fznso::Solution;
using fznso::Status;

constexpr FznsoType BOOL = fznso::Type{FznsoTypeBaseBool}.decision(true);
constexpr FznsoType INT = fznso::Type{FznsoTypeBaseInt}.decision(true);
constexpr FznsoType FLOAT = fznso::Type{FznsoTypeBaseFloat}.decision(true);

int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

OwnedValue num(std::int64_t v) { return OwnedValue{v}; }
OwnedValue list(std::vector<OwnedValue> items) { return OwnedValue{std::move(items)}; }

/// One run's answers, collected so the assertions can look at them afterwards.
struct Collected {
	std::vector<std::vector<double>> solutions;
	std::vector<std::pair<std::string, std::string>> messages;
	Status status{Status::Kind::Complete, {}};
};

/// Run `model`, reading `count` decisions out of every solution.
Collected solve(fznso::Library& lib, const LayeredModel& model, std::size_t count,
                const std::vector<std::pair<std::string, OwnedValue>>& options = {},
                std::size_t stop_after = 0) {
	fznso::DynSolver solver = lib.create_solver();
	for (const auto& o : options) {
		std::optional<std::string> error = solver.option_set(o.first, fznso::Value{o.second});
		check(!error.has_value(), error.has_value() ? error->c_str() : "option rejected");
	}
	Collected out;
	bool stop = false;
	out.status = solver.run(
		model,
		[&](const Solution& s) {
			std::vector<double> values;
			for (std::size_t i = 0; i < count; i++) {
				fznso::Value v = s[Decision{i}];
				switch (v.kind()) {
				case FznsoValueBool:
					values.push_back(v.as_bool() ? 1.0 : 0.0);
					break;
				case FznsoValueInt:
					values.push_back(static_cast<double>(v.as_int()));
					break;
				case FznsoValueFloat:
					values.push_back(v.as_float());
					break;
				default:
					values.push_back(-9999.0);
					break;
				}
			}
			out.solutions.push_back(std::move(values));
			if (stop_after != 0 && out.solutions.size() >= stop_after) {
				stop = true;
			}
		},
		[&](std::string_view scope, fznso::Value value) {
			out.messages.emplace_back(std::string(scope), value.kind() == FznsoValueString
			                                                  ? std::string(value.as_string())
			                                                  : std::string());
		},
		[&]() { return stop; });
	return out;
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

/// Post `Σ coeffs·xs ≤ bound`.
void lin_le(LayeredModel& m, std::vector<std::int64_t> coeffs, std::vector<OwnedValue> xs,
            std::int64_t bound) {
	std::vector<OwnedValue> cs;
	cs.reserve(coeffs.size());
	for (std::int64_t c : coeffs) {
		cs.push_back(num(c));
	}
	m.add_constraint("int_lin_le", {list(std::move(cs)), list(std::move(xs)), num(bound)});
}

// --- the checks ------------------------------------------------------------

/// `x + y = 10`, `x ≤ y`, over `0..10`.
void int_model(fznso::Library& lib) {
	LayeredModel m;
	Decision x = m.add_decision(INT, OwnedValue::int_range(0, 10), "x");
	Decision y = m.add_decision(INT, OwnedValue::int_range(0, 10), "y");
	m.add_constraint("int_lin_eq",
	                 {list({num(1), num(1)}), list({OwnedValue{x}, OwnedValue{y}}), num(10)});
	lin_le(m, {1, -1}, {OwnedValue{x}, OwnedValue{y}}, 0);

	Collected out = solve(lib, m, 2);
	check(out.status.complete(), "int model completes");
	check(out.solutions.size() == 1, "int model reports one solution");
	if (!out.solutions.empty()) {
		double a = out.solutions[0][0];
		double b = out.solutions[0][1];
		check(near(a + b, 10.0) && a <= b, "int model satisfies its constraints");
	}
}

/// A clause over three Booleans, all of which must be false but one.
void bool_model(fznso::Library& lib) {
	LayeredModel m;
	Decision a = m.add_decision(BOOL, OwnedValue{}, "a");
	Decision b = m.add_decision(BOOL, OwnedValue{}, "b");
	// `a ∨ b`, and `¬a`.
	m.add_constraint("bool_clause", {list({OwnedValue{a}, OwnedValue{b}}), list({})});
	m.add_constraint("bool_clause", {list({}), list({OwnedValue{a}})});

	Collected out = solve(lib, m, 2);
	check(out.status.complete(), "bool model completes");
	check(out.solutions.size() == 1, "bool model reports one solution");
	if (!out.solutions.empty()) {
		check(near(out.solutions[0][0], 0.0) && near(out.solutions[0][1], 1.0),
		      "bool model assigns a=false, b=true");
	}
}

/// A weighted count of Booleans, which is a row over binary columns and needs no
/// integer channel of its own.
void bool_lin_model(fznso::Library& lib) {
	LayeredModel m;
	Decision a = m.add_decision(BOOL, OwnedValue{}, "a");
	Decision b = m.add_decision(BOOL, OwnedValue{}, "b");
	Decision c = m.add_decision(BOOL, OwnedValue{}, "c");
	Decision n = m.add_decision(INT, OwnedValue::int_range(0, 3), "n");
	// `a + b + c = n`, and `n ≥ 2`.
	m.add_constraint("bool_lin_eq", {list({num(1), num(1), num(1)}),
	                                 list({OwnedValue{a}, OwnedValue{b}, OwnedValue{c}}),
	                                 OwnedValue{n}});
	lin_le(m, {-1}, {OwnedValue{n}}, -2);
	m.set_objective("int_minimize", OwnedValue{n});

	Collected out = solve(lib, m, 4);
	check(out.status.complete(), "bool_lin model completes");
	check(!out.solutions.empty(), "bool_lin model reports a solution");
	if (!out.solutions.empty()) {
		const std::vector<double>& s = out.solutions.back();
		check(near(s[0] + s[1] + s[2], s[3]), "bool_lin model counts correctly");
		check(near(s[3], 2.0), "bool_lin model minimises the count to 2");
	}
}

/// `bool_to_int` and `int_to_float` chained: three decisions, one column.
///
/// The pre-pass joins them before any column exists, so nothing here should cost
/// an equality row — and all three must read back the same value.
void aliasing(fznso::Library& lib) {
	LayeredModel m;
	Decision b = m.add_decision(BOOL, OwnedValue{}, "b");
	Decision i = m.add_decision(INT, OwnedValue::int_range(0, 1), "i");
	Decision f = m.add_decision(FLOAT, OwnedValue::float_range(0.0, 1.0), "f");
	m.add_constraint("bool_to_int", {OwnedValue{b}, OwnedValue{i}});
	m.add_constraint("int_to_float", {OwnedValue{i}, OwnedValue{f}});
	// Force the Boolean true through the integer view of the same column.
	lin_le(m, {-1}, {OwnedValue{i}}, -1);

	fznso::DynSolver solver = lib.create_solver();
	Collected out;
	out.status = solver.run(m, [&](const Solution& s) {
		out.solutions.push_back({s[b].kind() == FznsoValueBool && s[b].as_bool() ? 1.0 : 0.0,
		                         static_cast<double>(s[i].as_int()), s[f].as_float()});
	});
	check(out.status.complete(), "aliasing model completes");
	check(out.solutions.size() == 1, "aliasing model reports one solution");
	if (!out.solutions.empty()) {
		const std::vector<double>& s = out.solutions[0];
		check(near(s[0], 1.0) && near(s[1], 1.0) && near(s[2], 1.0),
		      "aliasing model agrees across all three views");
	}
	// Three decisions collapsed onto one column, and the two identities cost no
	// rows: that is the whole point of the pre-pass.
	check(solver.statistic("decisions").as_int() == 1, "aliasing model builds one column");
	check(solver.statistic("constraints").as_int() == 1, "aliasing model builds one row");
}

/// A domain with a hole is respected, not flattened to its bounds.
///
/// A MIP column is an interval, so `var {1, 9}` needs saying in rows. Reading
/// only the bounds looks right until the objective points into the gap: this
/// model's answer is 9, and a backend that saw `1..9` would say 2.
void domain_with_holes(fznso::Library& lib) {
	LayeredModel m;
	OwnedValue split{std::vector<fznso::Range<std::int64_t>>{{1, 1}, {9, 9}}};
	Decision x = m.add_decision(INT, std::move(split), "x");
	lin_le(m, {-1}, {OwnedValue{x}}, -2); // x >= 2
	m.set_objective("int_minimize", OwnedValue{x});

	Collected out = solve(lib, m, 1);
	check(out.status.complete(), "holey model completes");
	check(!out.solutions.empty() && near(out.solutions.back()[0], 9.0),
	      "a value in the hole is not chosen");
}

/// A continuous model, to check float columns and a float objective.
void float_model(fznso::Library& lib) {
	LayeredModel m;
	Decision x = m.add_decision(FLOAT, OwnedValue::float_range(0.0, 10.0), "x");
	m.add_constraint("float_lin_le",
	                 {list({OwnedValue{1.0}}), list({OwnedValue{x}}), OwnedValue{4.5}});
	m.set_objective("float_maximize", OwnedValue{x});

	Collected out = solve(lib, m, 1);
	check(out.status.complete(), "float model completes");
	check(!out.solutions.empty() && near(out.solutions.back()[0], 4.5),
	      "float model maximises to 4.5");
}

/// An unsatisfiable model is `Complete` with no solution reported.
void unsat_model(fznso::Library& lib) {
	LayeredModel m;
	Decision x = m.add_decision(INT, OwnedValue::int_range(0, 10), "x");
	lin_le(m, {1}, {OwnedValue{x}}, 2);
	lin_le(m, {-1}, {OwnedValue{x}}, -5);

	Collected out = solve(lib, m, 1);
	check(out.status.complete(), "unsat model completes");
	check(out.solutions.empty(), "unsat model reports no solution");
}

/// Asking to stop before the run starts gives no search at all.
void stop_signal(fznso::Library& lib) {
	LayeredModel m;
	Decision x = m.add_decision(INT, OwnedValue::int_range(0, 10), "x");
	lin_le(m, {1}, {OwnedValue{x}}, 10);

	fznso::DynSolver solver = lib.create_solver();
	std::size_t count = 0;
	Status status = solver.run(
		m, [&](const Solution&) { count++; },
		[](std::string_view, fznso::Value) {}, []() { return true; });
	check(status.kind == Status::Kind::Incomplete, "a pre-set stop gives an incomplete run");
	check(count == 0, "a pre-set stop reports no solution");
}

/// Asking to stop from inside `on_solution` ends the search.
///
/// HiGHS only reads `user_interrupt` from its interrupt callback — setting it
/// from the improving-solution callback trips an assertion inside HiGHS — so
/// this is what checks the two are wired to the right places.
void stop_mid_search(fznso::Library& lib) {
	LayeredModel m;
	std::vector<OwnedValue> xs;
	std::vector<OwnedValue> weights;
	std::vector<OwnedValue> profits;
	const int n = 200;
	for (int i = 0; i < n; i++) {
		xs.push_back(OwnedValue{m.add_decision(BOOL, OwnedValue{}, "x")});
		weights.push_back(num((i * 7919) % 1000 + 1));
		profits.push_back(num((i * 104729) % 997 + 1));
	}
	Decision total = m.add_decision(INT, OwnedValue::int_range(0, 1000000), "total");
	m.add_constraint("bool_lin_le", {list(weights), list(xs), num(50000)});
	m.add_constraint("bool_lin_eq", {list(profits), list(xs), OwnedValue{total}});
	m.set_objective("int_maximize", OwnedValue{total});

	Collected out = solve(lib, m, 0, {{"intermediate", OwnedValue{true}}}, /*stop_after=*/1);
	check(out.status.kind == Status::Kind::Incomplete, "stopping mid-search is incomplete");
	check(!out.solutions.empty(), "the solution that triggered the stop is still reported");
}

/// A bad model fails the run with a message naming the constraint.
void unknown_constraint(fznso::Library& lib) {
	LayeredModel m;
	Decision x = m.add_decision(INT, OwnedValue::int_range(0, 10), "x");
	m.add_constraint("int_all_different", {list({OwnedValue{x}})});

	Collected out = solve(lib, m, 1);
	check(out.status.failed(), "an undeclared constraint fails the run");
	check(out.status.error.find("int_all_different") != std::string::npos,
	      "the failure names the constraint");
}

/// The type says what a variable is, not the domain.
void type_drives_the_variable(fznso::Library& lib) {
	LayeredModel m;
	Decision b = m.add_decision(BOOL, OwnedValue{}, "b");
	Decision x = m.add_decision(INT, OwnedValue::int_range(0, 1), "x");
	m.add_constraint("bool_clause", {list({OwnedValue{b}}), list({})});
	lin_le(m, {-1}, {OwnedValue{x}}, 0);

	Collected out = solve(lib, m, 2);
	check(!out.solutions.empty(), "typed model reports a solution");
	fznso::DynSolver solver = lib.create_solver();
	Status status = solver.run(m, [&](const Solution& s) {
		check(s[b].kind() == FznsoValueBool, "a bool decision reads back as a bool");
		check(s[x].kind() == FznsoValueInt, "an int decision reads back as an int");
	});
	check(status.complete(), "typed model completes");
}

/// A declared constraint arrives under its `fzn_` name too.
///
/// Declaring `bool_lin_eq` makes both `bool_lin_eq` and `fzn_bool_lin_eq`
/// native, and a body in a solver's own library has to name the `fzn_` form to
/// avoid a rewrite loop — so that is the name the call is emitted under.
void fzn_prefixed_idents(fznso::Library& lib) {
	LayeredModel m;
	Decision x = m.add_decision(INT, OwnedValue::int_range(0, 10), "x");
	m.add_constraint("fzn_int_lin_le", {list({num(-1)}), list({OwnedValue{x}}), num(-4)});
	Collected out = solve(lib, m, 1);
	check(out.status.complete(), "an `fzn_`-prefixed constraint is accepted");
	check(!out.solutions.empty() && out.solutions[0][0] >= 4.0,
	      "an `fzn_`-prefixed constraint is posted");
}

/// Every declared constraint must be one the solver can actually post.
///
/// The compile and size gates are both happy with a name that fails at run time,
/// so this is the check that catches an over-broad declaration.
void declared_constraints_post(fznso::Library& lib) {
	FznsoConstraintList declared = lib.constraint_types();
	check(declared.len > 0, "the solver declares something");
	std::set<std::string> seen;
	for (std::size_t i = 0; i < declared.len; i++) {
		const FznsoConstraintType& c = declared.constraints[i];
		std::string ident{c.ident.ptr, c.ident.len};
		check(seen.insert(ident).second, "no constraint is declared twice");
		check(c.arg_len > 0, "a declared constraint takes arguments");
	}
	// Every name the fixtures above post is declared.
	for (const char* ident : {"int_lin_eq", "int_lin_le", "float_lin_eq", "float_lin_le",
	                          "bool_lin_eq", "bool_lin_le", "bool_clause", "bool_to_int",
	                          "int_to_float"}) {
		check(seen.count(ident) == 1, ident);
	}
	// No MIP posts any of these as one row, whatever else it can do, so none of
	// them may be declared by any backend. `int_times` is deliberately not in
	// the list: a backend with quadratic rows declares it, and one without does
	// not, so it says nothing about every backend.
	for (const char* ident : {"int_lin_ne", "int_lin_le_reif", "int_lin_eq_reif",
	                          "bool_array_and", "bool_array_xor", "all_solutions"}) {
		check(seen.count(ident) == 0, ident);
	}
}

/// Options round-trip, and a rejected value leaves the previous one in place.
void options(fznso::Library& lib) {
	fznso::DynSolver solver = lib.create_solver();
	check(!solver.option_set("time_limit", fznso::Value{std::int64_t{5000}}).has_value(),
	      "time_limit accepts a positive value");
	check(solver.option_get("time_limit").as_int() == 5000, "time_limit reads back");
	check(solver.option_set("time_limit", fznso::Value{std::int64_t{0}}).has_value(),
	      "time_limit rejects a non-positive value");
	check(solver.option_get("time_limit").as_int() == 5000,
	      "a rejected value leaves the previous one in place");
	check(solver.option_set("nonexistent_option", fznso::Value{true}).has_value(),
	      "an unknown option is rejected");
	OwnedValue absent;
	check(!solver.option_set("time_limit", fznso::Value{absent}).has_value(),
	      "time_limit accepts the absent value");
	check(solver.option_get("time_limit").kind() == FznsoValueAbsent,
	      "time_limit reads back absent");
}

/// Solver-level statistics survive a later read, and are reported after a run.
void statistics(fznso::Library& lib) {
	LayeredModel m;
	Decision x = m.add_decision(INT, OwnedValue::int_range(0, 10), "x");
	lin_le(m, {-1}, {OwnedValue{x}}, -3);
	m.set_objective("int_minimize", OwnedValue{x});

	fznso::DynSolver solver = lib.create_solver();
	Status status = solver.run(m, [](const Solution&) {});
	check(status.complete(), "statistics model completes");
	fznso::Value decisions = solver.statistic("decisions");
	fznso::Value constraints = solver.statistic("constraints");
	// Read the first one again: an earlier statistic must survive a later read.
	check(decisions.as_int() == 1, "one column");
	check(constraints.as_int() == 1, "one row");
	check(solver.statistic("int_objective").as_int() == 3, "the objective is reported");
	check(solver.statistic("nonexistent_statistic").kind() == FznsoValueAbsent,
	      "an unknown statistic is absent");
}

/// One solver, run twice, with a layer pushed in between.
///
/// The point is the *carrying*: the solver keeps what the first run ended with
/// and offers it to the backend as a starting point for the second. A start is
/// advice, so the second answer must obey the new layer whatever was carried —
/// which is what this checks, because a solver that reported its stale solution
/// would pass every other test in this file.
void incremental_rerun(fznso::Library& lib) {
	LayeredModel m;
	Decision x = m.add_decision(INT, OwnedValue::int_range(0, 10), "x");
	Decision y = m.add_decision(INT, OwnedValue::int_range(0, 10), "y");
	lin_le(m, {1, 1}, {OwnedValue{x}, OwnedValue{y}}, 10);
	m.set_objective("int_maximize", OwnedValue{x});

	fznso::DynSolver solver = lib.create_solver();

	std::vector<double> first;
	Status s1 = solver.run(m, [&](const Solution& s) {
		first = {static_cast<double>(s[x].as_int()), static_cast<double>(s[y].as_int())};
	});
	check(s1.complete(), "first run completes");
	check(!first.empty() && near(first[0], 10.0), "first run maximises x to 10");

	// A layer the solver has not seen, ruling out what it just answered. Layers
	// 0 and 1 (the base and this one) are all there is, and only layer 0 is
	// unchanged.
	m.push_layer();
	lin_le(m, {1}, {OwnedValue{x}}, 6); // x <= 6
	m.set_unchanged(1);

	std::vector<double> second;
	Status s2 = solver.run(m, [&](const Solution& s) {
		second = {static_cast<double>(s[x].as_int()), static_cast<double>(s[y].as_int())};
	});
	check(s2.complete(), "second run completes");
	check(!second.empty() && near(second[0], 6.0),
	      "second run obeys the layer added since the first");

	// And once more with the layer retracted: the carried values are feasible
	// again, and the answer goes back to what it was.
	m.pop_layer();
	m.set_unchanged(1);
	std::vector<double> third;
	Status s3 = solver.run(m, [&](const Solution& s) {
		third = {static_cast<double>(s[x].as_int()), static_cast<double>(s[y].as_int())};
	});
	check(s3.complete(), "third run completes");
	check(!third.empty() && near(third[0], 10.0), "third run recovers the original optimum");
}

/// A layer that brings its own decisions, and one that forces a full rebuild.
///
/// Extending covers the ordinary case — a layer of rows over columns that are
/// already there. These are the two it does not: a layer with new columns, and
/// a layer whose `bool_to_int` joins two decisions that already have a column
/// apiece, which cannot be done by adding and has to start over. Both must give
/// the same answer as building from nothing, which is all the caller can see.
void incremental_shapes(fznso::Library& lib) {
	{
		LayeredModel m;
		Decision x = m.add_decision(INT, OwnedValue::int_range(0, 10), "x");
		lin_le(m, {-1}, {OwnedValue{x}}, -2); // x >= 2
		m.set_objective("int_minimize", OwnedValue{x});

		fznso::DynSolver solver = lib.create_solver();
		std::int64_t first = -1;
		Status s1 = solver.run(m, [&](const Solution& s) { first = s[x].as_int(); });
		check(s1.complete() && first == 2, "layer 0 minimises x to 2");

		// A layer of its own decisions, tied to the old one.
		m.push_layer();
		Decision y = m.add_decision(INT, OwnedValue::int_range(0, 10), "y");
		lin_le(m, {-1, 1}, {OwnedValue{y}, OwnedValue{x}}, -3); // y >= x + 3
		m.set_unchanged(1);

		std::int64_t gx = -1;
		std::int64_t gy = -1;
		Status s2 = solver.run(m, [&](const Solution& s) {
			gx = s[x].as_int();
			gy = s[y].as_int();
		});
		check(s2.complete() && gx == 2 && gy >= 5, "a layer of new decisions is added, not rebuilt");
	}
	{
		LayeredModel m;
		Decision b = m.add_decision(BOOL, OwnedValue{}, "b");
		Decision i = m.add_decision(INT, OwnedValue::int_range(0, 1), "i");
		lin_le(m, {-1}, {OwnedValue{i}}, -1); // i >= 1
		m.set_objective("int_minimize", OwnedValue{i});

		fznso::DynSolver solver = lib.create_solver();
		Status s1 = solver.run(m, [](const Solution&) {});
		check(s1.complete(), "two separate columns to begin with");

		// Now say they were the same column all along. Two classes that each
		// own a column cannot be merged in place, so this must start over —
		// and still answer.
		m.push_layer();
		m.add_constraint("bool_to_int", {OwnedValue{b}, OwnedValue{i}});
		m.set_unchanged(1);

		bool bv = false;
		std::int64_t iv = -1;
		Status s2 = solver.run(m, [&](const Solution& s) {
			bv = s[b].as_bool();
			iv = s[i].as_int();
		});
		check(s2.complete() && bv && iv == 1, "a late join rebuilds and still agrees");
		check(solver.statistic("decisions").as_int() == 1, "the late join really did merge them");
	}
}

/// `x · y = 6` with `x <= 2`, for a backend that declares `int_times`.
///
/// The product of two decisions against a *value*: there is no third decision
/// to hold the result, so it is a quadratic row with no linear term rather than
/// a linearisable one. Skipped where `int_times` is not declared, since the
/// constraint then never reaches the backend at all.
void product_against_a_value(fznso::Library& lib) {
	FznsoConstraintList declared = lib.constraint_types();
	bool quadratic = false;
	for (std::size_t i = 0; i < declared.len; i++) {
		std::string ident{declared.constraints[i].ident.ptr, declared.constraints[i].ident.len};
		quadratic = quadratic || ident == "int_times";
	}
	if (!quadratic) {
		return;
	}
	LayeredModel m;
	Decision x = m.add_decision(INT, OwnedValue::int_range(1, 6), "x");
	Decision y = m.add_decision(INT, OwnedValue::int_range(1, 6), "y");
	m.add_constraint("int_times", {OwnedValue{x}, OwnedValue{y}, num(6)});
	lin_le(m, {1}, {OwnedValue{x}}, 2);
	m.set_objective("int_maximize", OwnedValue{x});

	Collected out = solve(lib, m, 2);
	check(out.status.complete(), "a product against a value completes");
	check(!out.solutions.empty(), "a product against a value reports a solution");
	if (!out.solutions.empty()) {
		const std::vector<double>& sol = out.solutions.back();
		check(near(sol[0] * sol[1], 6.0), "the product holds");
		check(near(sol[0], 2.0), "and the objective is maximised within it");
	}
}

/// `x + y <= 10` maximised lexicographically: `x` first, then `y`.
///
/// The point is the *ranking*. A weighted sum would let a large `y` buy a
/// smaller `x`; a lexicographic objective may not, so the only answer is the
/// largest `x` the constraint allows and the best `y` given it. Skipped where
/// the objective is not declared.
void lex_objective(fznso::Library& lib) {
	FznsoObjectiveList declared = lib.objectives();
	bool lex = false;
	for (std::size_t i = 0; i < declared.len; i++) {
		std::string ident{declared.objectives[i].ident.ptr, declared.objectives[i].ident.len};
		lex = lex || ident == "int_lex_maximize";
	}
	if (!lex) {
		return;
	}
	LayeredModel m;
	Decision x = m.add_decision(INT, OwnedValue::int_range(0, 6), "x");
	Decision y = m.add_decision(INT, OwnedValue::int_range(0, 6), "y");
	lin_le(m, {1, 1}, {OwnedValue{x}, OwnedValue{y}}, 10);
	m.set_objective("int_lex_maximize", list({OwnedValue{x}, OwnedValue{y}}));

	Collected out = solve(lib, m, 2);
	check(out.status.complete(), "a lexicographic objective completes");
	check(!out.solutions.empty(), "a lexicographic objective reports a solution");
	if (!out.solutions.empty()) {
		const std::vector<double>& sol = out.solutions.back();
		// `x` is maximised first, so it takes its whole domain and `y` gets
		// what is left. A weighted sum over these could prefer x=5,y=5.
		check(near(sol[0], 6.0), "the first objective is maximised");
		check(near(sol[1], 4.0), "and the second only within what it left");
	}
}

/// Whether an option is the core's rather than a backend's.
bool core_option(const std::string& ident) {
	for (const char* core : {"intermediate", "threads", "time_limit", "random_seed", "verbose",
	                         "all_solutions"}) {
		if (ident == core) {
			return true;
		}
	}
	return false;
}

/// The backend's library-path option, if it has one.
///
/// A backend that opens its solver at run time declares `<solver>_dll`, the
/// counterpart of MiniZinc's `--<solver>-dll`; one linked against its solver
/// has nothing to point anywhere.
std::string dll_option(fznso::Library& lib) {
	FznsoOptionList options = lib.options();
	for (std::size_t i = 0; i < options.len; i++) {
		std::string ident{options.options[i].ident.ptr, options.options[i].ident.len};
		if (ident.size() > 4 && ident.compare(ident.size() - 4, 4, "_dll") == 0) {
			return ident;
		}
	}
	return {};
}

/// One of the backend's own float options, whichever it is.
///
/// The core's options are answered above the backend, so setting one says
/// nothing about whether the backend reached its solver.
std::string backend_float_option(fznso::Library& lib) {
	FznsoOptionList options = lib.options();
	for (std::size_t i = 0; i < options.len; i++) {
		std::string ident{options.options[i].ident.ptr, options.options[i].ident.len};
		if (!core_option(ident) && options.options[i].arg_ty.base == FznsoTypeBaseFloat) {
			return ident;
		}
	}
	return {};
}

/// What a backend that cannot reach its solver still owes a consumer.
///
/// A backend loaded at run time is built, loaded, and asked what it can do on a
/// machine that has none of its solver installed. The five lists and
/// `solver_create` are how a consumer decides whether to use it at all, so they
/// have to answer there — and neither can carry a message. The two calls that
/// can are `option_set` and `run`; both must report it, both must say the same
/// thing, and it must name what was tried, because a list of paths that did not
/// open is the difference between a fixed installation and a bug report.
///
/// Pointing the backend at a library that is not there is that state exactly,
/// and is reachable whether or not this machine has the real thing.
void unavailable_backend(fznso::Library& lib, const std::string& dll) {
	check(lib.constraint_types().len > 0, "an unavailable backend still declares constraints");
	check(lib.decision_types().len > 0, "an unavailable backend still declares decision types");
	check(lib.objectives().len > 0, "an unavailable backend still declares objectives");
	check(lib.options().len > 0, "an unavailable backend still declares options");
	check(lib.statistics().len > 0, "an unavailable backend still declares statistics");

	std::string float_option = backend_float_option(lib);
	check(!float_option.empty(), "a run-time-loaded backend declares an option of its own");
	if (float_option.empty()) {
		return;
	}

	fznso::DynSolver solver = lib.create_solver();
	const std::string nowhere = "/nonexistent/fznso-no-such-solver-library";
	OwnedValue path{nowhere};
	check(!solver.option_set(dll, fznso::Value{path}).has_value(),
	      "the library path is settable before anything has been loaded");

	std::optional<std::string> rejected = solver.option_set(float_option, fznso::Value{1e-6});
	check(rejected.has_value(), "option_set reports that the solver could not be loaded");

	LayeredModel m;
	Decision x = m.add_decision(INT, OwnedValue::int_range(0, 1), "x");
	lin_le(m, {1}, {OwnedValue{x}}, 1);
	Status status = solver.run(m, [](const Solution&) {});
	check(status.failed(), "run reports that the solver could not be loaded");
	check(rejected.has_value() && status.error == *rejected,
	      "option_set and run report the same failure");
	check(status.error.find(nowhere) != std::string::npos, "the failure names what was tried");
}

/// Why the backend cannot solve at all, if it cannot.
///
/// Everything below this point in the suite assumes a solver that answers, and
/// a run-time-loaded backend on a machine without its solver does not. What it
/// owes a consumer anyway is `unavailable_backend`, which has just been run
/// against it.
std::optional<std::string> unavailable(fznso::Library& lib) {
	LayeredModel m;
	Decision x = m.add_decision(INT, OwnedValue::int_range(0, 1), "x");
	lin_le(m, {1}, {OwnedValue{x}}, 1);
	fznso::DynSolver solver = lib.create_solver();
	Status status = solver.run(m, [](const Solution&) {});
	if (status.failed()) {
		return status.error;
	}
	return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
	if (argc != 2) {
		std::fprintf(stderr, "usage: %s <solver library>\n", argv[0]);
		return 2;
	}
	try {
		fznso::Library lib{argv[1]};
		std::string dll = dll_option(lib);
		if (!dll.empty()) {
			unavailable_backend(lib, dll);
		}
		if (std::optional<std::string> why = unavailable(lib)) {
			// This machine has none of the solver, so nothing below can run.
			std::printf("solver not installed, checked what it must answer anyway: %s\n",
			            why->c_str());
			return failures == 0 ? 0 : 1;
		}
		int_model(lib);
		product_against_a_value(lib);
		lex_objective(lib);
		bool_model(lib);
		bool_lin_model(lib);
		aliasing(lib);
		domain_with_holes(lib);
		float_model(lib);
		unsat_model(lib);
		stop_signal(lib);
		stop_mid_search(lib);
		unknown_constraint(lib);
		type_drives_the_variable(lib);
		fzn_prefixed_idents(lib);
		declared_constraints_post(lib);
		options(lib);
		statistics(lib);
		incremental_rerun(lib);
		incremental_shapes(lib);
	} catch (const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
	if (failures != 0) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}

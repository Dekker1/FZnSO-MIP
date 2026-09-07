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
	// A constraint the backend cannot post must not be declared.
	for (const char* ident : {"int_lin_ne", "int_lin_le_reif", "int_lin_eq_reif",
	                          "bool_array_and", "bool_array_xor", "int_times", "all_solutions"}) {
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

} // namespace

int main(int argc, char** argv) {
	if (argc != 2) {
		std::fprintf(stderr, "usage: %s <solver library>\n", argv[0]);
		return 2;
	}
	try {
		fznso::Library lib{argv[1]};
		int_model(lib);
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

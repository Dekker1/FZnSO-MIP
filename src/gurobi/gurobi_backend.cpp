// Gurobi behind the shared MIP core, loaded at run time.
//
// Unlike HiGHS this is not linked in. The library is found and opened wherever
// it happens to be installed, the way MiniZinc's own
// `solvers/MIP/MIP_gurobi_wrap.cpp` finds it, and nothing here comes from the
// Gurobi SDK: the constants and the two opaque handles below are the whole of
// the interface, so this builds, loads and answers on a machine that has no
// Gurobi at all.
//
// Which is why nothing loads eagerly. The five capability lists and
// `solver_create` are how a consumer decides whether to use this solver, so
// they have to work before anything is installed, and neither can carry a
// message. The two that can are `option_set` and `solve` — so the load is
// attempted at the first of those to need it, and whatever went wrong is kept
// and returned from both.

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include "mip_backend.hh"
#include "mip_solver.hh"
#include "signatures.hh"

namespace {

using namespace fznso_mip;

// --- the Gurobi interface, without the Gurobi SDK ---------------------------
//
// Two opaque handles and the constants this file names. Gurobi's own C API uses
// `__stdcall` on Windows, and a mismatch there is a corrupted stack rather than
// a link error, so it is spelled out.

#ifndef _WIN32
#define __stdcall
#endif

struct _GRBenv;
struct _GRBmodel;
using GRBenv = _GRBenv;
using GRBmodel = _GRBmodel;

constexpr char kLessEqual = '<';
constexpr char kGreaterEqual = '>';
constexpr char kEqual = '=';
constexpr char kContinuous = 'C';
constexpr char kInteger = 'I';
constexpr int kMinimize = 1;
constexpr int kMaximize = -1;
/// Gurobi's own infinity, which is *not* `HUGE_VAL`: a bound at or above this
/// is infinite to Gurobi and a finite number to everything else.
constexpr double kInfinity = 1e100;

constexpr int kStatusOptimal = 2;
constexpr int kStatusInfeasible = 3;
constexpr int kStatusInfOrUnbounded = 4;
constexpr int kStatusUnbounded = 5;

constexpr int kCallbackMip = 3;
constexpr int kCallbackMipSol = 4;
constexpr int kCallbackMessage = 6;
constexpr int kCallbackMipObjBound = 3001;
constexpr int kCallbackMipNodesLeft = 3005;
constexpr int kCallbackMipSolSolution = 4001;
constexpr int kCallbackMipSolObjective = 4002;
constexpr int kCallbackMessageString = 6001;

/// How many solutions the pool may hold, which is Gurobi's own maximum for the
/// parameter: `all_solutions` asks for every one that ties with the optimum.
constexpr int kPoolLimit = 2000000000;

/// The symbols this backend resolves out of whatever library it opened.
struct Symbols {
	int(__stdcall* GRBemptyenv)(GRBenv** env) = nullptr;
	int(__stdcall* GRBemptyenvinternal)(GRBenv** env, int major, int minor, int tech) = nullptr;
	int(__stdcall* GRBstartenv)(GRBenv* env) = nullptr;
	void(__stdcall* GRBfreeenv)(GRBenv* env) = nullptr;
	const char*(__stdcall* GRBgeterrormsg)(GRBenv* env) = nullptr;
	GRBenv*(__stdcall* GRBgetenv)(GRBmodel* model) = nullptr;
	int(__stdcall* GRBsetintparam)(GRBenv* env, const char* name, int value) = nullptr;
	int(__stdcall* GRBsetdblparam)(GRBenv* env, const char* name, double value) = nullptr;

	int(__stdcall* GRBnewmodel)(GRBenv* env, GRBmodel** model, const char* name, int numvars,
	                            double* obj, double* lb, double* ub, char* vtype,
	                            char** varnames) = nullptr;
	int(__stdcall* GRBfreemodel)(GRBmodel* model) = nullptr;
	int(__stdcall* GRBupdatemodel)(GRBmodel* model) = nullptr;
	int(__stdcall* GRBoptimize)(GRBmodel* model) = nullptr;
	void(__stdcall* GRBterminate)(GRBmodel* model) = nullptr;
	int(__stdcall* GRBwrite)(GRBmodel* model, const char* filename) = nullptr;

	int(__stdcall* GRBaddvars)(GRBmodel* model, int numvars, int numnz, const int* vbeg,
	                           const int* vind, const double* vval, const double* obj,
	                           const double* lb, const double* ub, const char* vtype,
	                           char** varnames) = nullptr;
	int(__stdcall* GRBaddconstr)(GRBmodel* model, int numnz, const int* cind, const double* cval,
	                             char sense, double rhs, const char* name) = nullptr;
	int(__stdcall* GRBaddgenconstrIndicator)(GRBmodel* model, const char* name, int binvar,
	                                         int binval, int nvars, const int* ind,
	                                         const double* val, char sense,
	                                         double rhs) = nullptr;
	int(__stdcall* GRBaddqconstr)(GRBmodel* model, int numlnz, const int* lind, const double* lval,
	                              int numqnz, const int* qrow, const int* qcol, const double* qval,
	                              char sense, double rhs, const char* name) = nullptr;
	int(__stdcall* GRBdelvars)(GRBmodel* model, int numdel, const int* ind) = nullptr;
	int(__stdcall* GRBdelconstrs)(GRBmodel* model, int numdel, const int* ind) = nullptr;
	int(__stdcall* GRBdelgenconstrs)(GRBmodel* model, int numdel, const int* ind) = nullptr;
	int(__stdcall* GRBdelqconstrs)(GRBmodel* model, int numdel, const int* ind) = nullptr;

	int(__stdcall* GRBgetintattr)(GRBmodel* model, const char* name, int* value) = nullptr;
	int(__stdcall* GRBsetintattr)(GRBmodel* model, const char* name, int value) = nullptr;
	int(__stdcall* GRBgetdblattr)(GRBmodel* model, const char* name, double* value) = nullptr;
	int(__stdcall* GRBgetdblattrarray)(GRBmodel* model, const char* name, int first, int len,
	                                   double* values) = nullptr;
	int(__stdcall* GRBsetdblattrlist)(GRBmodel* model, const char* name, int len, const int* ind,
	                                  const double* values) = nullptr;
	int(__stdcall* GRBsetobjectiven)(GRBmodel* model, int index, int priority, double weight,
	                                 double abstol, double reltol, const char* name,
	                                 double constant, int lnz, const int* lind,
	                                 const double* lval) = nullptr;

	int(__stdcall* GRBsetcallbackfunc)(GRBmodel* model,
	                                   int(__stdcall* cb)(GRBmodel*, void*, int, void*),
	                                   void* usrdata) = nullptr;
	int(__stdcall* GRBcbget)(void* cbdata, int where, int what, void* result) = nullptr;
};

// --- opening the library ----------------------------------------------------

void* dll_open(const std::string& file) {
#ifdef _WIN32
	return static_cast<void*>(LoadLibraryA(file.c_str()));
#else
	return dlopen(file.c_str(), RTLD_NOW);
#endif
}

void* dll_symbol(void* dll, const char* name) {
#ifdef _WIN32
	return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(dll), name));
#else
	return dlsym(dll, name);
#endif
}

/// A bare name for the platform loader to resolve on its own search path.
std::string bare(const std::string& major) {
#ifdef _WIN32
	return "gurobi" + major + ".dll";
#elif defined(__APPLE__)
	return "libgurobi" + major + ".dylib";
#else
	return "libgurobi" + major + ".so";
#endif
}

/// Where to look, newest first.
///
/// The version list is MiniZinc's `gurobi_dlls()` verbatim, including the few
/// above the current release: a Gurobi installed after this was built still has
/// to load. Each major version is tried first as a bare name, so a library on
/// the loader's own path wins over any guessed absolute one.
std::vector<std::string> candidates() {
	static const char* versions[] = {
		"1303", "1302", "1301", // Potential future versions which should load correctly
		"1300", "1203", "1202", "1201", "1200", "1103", "1102", "1101", "1100",
		"1003", "1002", "1001", "1000", "952",  "951",  "950",  "912",  "911",
		"910",  "903",  "902",  "901",  "900",  "811",  "810",  "801",  "800",
		"752",  "751",  "750",  "702",  "701",  "700",  "652",  "651",  "650"};
	std::vector<std::string> paths;
	std::string last_major;
	for (const char* v : versions) {
		std::string version{v};
		std::string major = version.substr(0, version.size() - 1);
		if (major != last_major) {
			paths.push_back(bare(major));
			last_major = major;
		}
#ifdef _WIN32
		paths.push_back("C:\\gurobi" + version + "\\win64\\bin\\gurobi" + major + ".dll");
#elif defined(__APPLE__)
		paths.push_back("/Library/gurobi" + version + "/mac64/lib/libgurobi" + major + ".dylib");
		paths.push_back("/Library/gurobi" + version + "/macos_universal2/lib/libgurobi" + major +
		                ".dylib");
#else
		paths.push_back("/opt/gurobi" + version + "/linux64/lib/libgurobi" + major + ".so");
#endif
	}
	return paths;
}

/// Gurobi does everything HiGHS does and three things more, each of which costs
/// a method below. The declared lists and the constraint dispatch both read
/// this, so they cannot drift apart.
constexpr Capabilities kGurobiCaps{/*indicators=*/true,  /*quadratic=*/true,
                                   /*solution_pool=*/true, /*floats=*/true,
                                   /*incremental=*/true,   /*lexicographic=*/true};

char grb_sense(RowSense sense) {
	switch (sense) {
	case RowSense::Le:
		return kLessEqual;
	case RowSense::Ge:
		return kGreaterEqual;
	default:
		return kEqual;
	}
}

/// What Gurobi's callback is given, since it cannot be a member function.
struct CallbackState {
	const Symbols* fn = nullptr;
	MipSink* sink = nullptr;
	std::size_t streamed = 0;
	bool intermediate = false;
	bool log = false;
	bool have_bound = false;
	double bound = 0.0;
	/// Nodes still to explore, which is only readable from the callback.
	std::int64_t open_nodes = 0;
	/// Where `GRB_CB_MIPSOL_SOL` writes: one double per column, reused.
	std::vector<double> values;
};

class GurobiBackend final : public MipBackend {
public:
	~GurobiBackend() override {
		if (model_ != nullptr) {
			fn_.GRBfreemodel(model_);
		}
		if (env_ != nullptr) {
			fn_.GRBfreeenv(env_);
		}
		// The library handle is deliberately not closed: an environment freed
		// above may still be unwinding inside it, and MiniZinc's own wrapper
		// gave up on closing for the same reason.
	}

	Capabilities capabilities() const override { return kGurobiCaps; }

	/// Answered without the library, because the model is built through this
	/// before anything has been loaded — and Gurobi's infinity is a finite
	/// `1e100`, so guessing `HUGE_VAL` here would silently produce bounds
	/// Gurobi treats as real.
	double infinity() const override { return kInfinity; }

	void reset() override {
		if (!available()) {
			return;
		}
		if (model_ != nullptr) {
			call(fn_.GRBfreemodel(model_), "unable to discard the model");
			model_ = nullptr;
		}
		row_kind_.clear();
		new_model();
	}

	void add_columns(std::size_t n, const double* obj, const double* lb, const double* ub,
	                 const ColKind* kind) override {
		if (!ready() || n == 0) {
			return;
		}
		std::vector<char> types(n);
		for (std::size_t i = 0; i < n; i++) {
			types[i] = kind[i] == ColKind::Integer ? kInteger : kContinuous;
		}
		call(fn_.GRBaddvars(model_, static_cast<int>(n), 0, nullptr, nullptr, nullptr, obj, lb, ub,
		                    types.data(), nullptr),
		     "unable to add columns");
		// Gurobi's model is lazy: a column just added is invisible to a query
		// or an attribute change until the model is updated. `set_start` sets
		// an attribute on these columns and `truncate` counts them, so this is
		// the one place the update cannot be left to `GRBoptimize`.
		call(fn_.GRBupdatemodel(model_), "unable to update the model");
	}

	void add_row(std::size_t nnz, const int* idx, const double* val, RowSense sense,
	             double rhs) override {
		if (!ready()) {
			return;
		}
		// No update: nothing between here and the solve reads a row back, and
		// `truncate` does its own.
		call(fn_.GRBaddconstr(model_, static_cast<int>(nnz), idx, val, grb_sense(sense), rhs,
		                      nullptr),
		     "unable to add a row");
		row_kind_.push_back(kLinear);
	}

	void add_indicator_row(int bin_col, bool on_value, std::size_t nnz, const int* idx,
	                       const double* val, RowSense sense, double rhs) override {
		if (!ready()) {
			return;
		}
		call(fn_.GRBaddgenconstrIndicator(model_, nullptr, bin_col, on_value ? 1 : 0,
		                                  static_cast<int>(nnz), idx, val, grb_sense(sense), rhs),
		     "unable to add an indicator row");
		row_kind_.push_back(kIndicator);
	}

	void add_quadratic_row(int out_col, int a_col, int b_col, double rhs) override {
		if (!ready()) {
			return;
		}
		// `a·b − out = rhs`, which is how MiniZinc's own wrapper says it, with
		// no linear part at all when the product is against a value.
		const double linear = -1.0;
		const double product = 1.0;
		call(fn_.GRBaddqconstr(model_, out_col >= 0 ? 1 : 0, &out_col, &linear, 1, &a_col, &b_col,
		                       &product, kEqual, rhs, nullptr),
		     "unable to add a quadratic row");
		row_kind_.push_back(kQuadratic);
	}

	void set_lex_objective(std::size_t n, const int* cols, bool maximise) override {
		if (!ready()) {
			return;
		}
		// One objective per column, ranked by priority: Gurobi optimises the
		// highest first and then each next one subject to the value it proved,
		// which is what the registry's `_lex_` ordering asks for. The sense is
		// the model's, so a maximisation is stated once here rather than by
		// negating every coefficient.
		call(fn_.GRBsetintattr(model_, "ModelSense", maximise ? kMaximize : kMinimize),
		     "unable to set the objective sense");
		const double one = 1.0;
		for (std::size_t i = 0; i < n; i++) {
			call(fn_.GRBsetobjectiven(model_, static_cast<int>(i),
			                          static_cast<int>(n - i) - 1, 1.0, 0.0, 0.0, nullptr, 0.0, 1,
			                          &cols[i], &one),
			     "unable to set a lexicographic objective");
		}
	}

	void set_objective_sense(bool maximise) override {
		if (!ready()) {
			return;
		}
		call(fn_.GRBsetintattr(model_, "ModelSense", maximise ? kMaximize : kMinimize),
		     "unable to set the objective sense");
	}

	void truncate(std::size_t first_row, std::size_t first_col) override {
		if (!ready()) {
			return;
		}
		// Rows added since the last update are not counted yet, so the model
		// has to be made current before either count is believed.
		call(fn_.GRBupdatemodel(model_), "unable to update the model");
		// The core numbers every row in one sequence, but Gurobi keeps linear,
		// indicator and quadratic constraints in three, so `first_row` indexes
		// none of them. Each sequence loses as many rows as the doomed suffix
		// holds of its kind, and loses them from its own end.
		std::size_t doomed[3] = {0, 0, 0};
		for (std::size_t r = first_row; r < row_kind_.size(); r++) {
			doomed[row_kind_[r]]++;
		}
		row_kind_.resize(std::min(first_row, row_kind_.size()));
		drop_last(doomed[kLinear], "NumConstrs", fn_.GRBdelconstrs, "unable to delete rows");
		drop_last(doomed[kIndicator], "NumGenConstrs", fn_.GRBdelgenconstrs,
		          "unable to delete indicator rows");
		drop_last(doomed[kQuadratic], "NumQConstrs", fn_.GRBdelqconstrs,
		          "unable to delete quadratic rows");
		int columns = 0;
		call(fn_.GRBgetintattr(model_, "NumVars", &columns), "unable to delete columns");
		if (static_cast<std::size_t>(columns) > first_col) {
			drop_last(static_cast<std::size_t>(columns) - first_col, "NumVars", fn_.GRBdelvars,
			          "unable to delete columns");
		}
		// And again, because the deletions are pending too and everything
		// posted after this counts on the indices they leave behind.
		call(fn_.GRBupdatemodel(model_), "unable to update the model");
	}

	void set_start(std::size_t n, const int* cols, const double* values) override {
		if (!ready() || n == 0) {
			return;
		}
		// An attribute over columns that are already in the model, which
		// `add_columns` saw to. Advice only: Gurobi repairs or discards a start
		// it cannot use, so an infeasible one — the usual case, since the
		// commonest incremental pattern cuts off what was just found — costs
		// the search nothing.
		call(fn_.GRBsetdblattrlist(model_, "Start", static_cast<int>(n), cols, values),
		     "unable to set the starting solution");
	}

	RunOutcome solve(const MipOptions& options, MipSink& sink) override;

	std::optional<std::string> option_set(std::string_view name, fznso::Value value) override {
		if (name == "gurobi_dll") {
			if (value.kind() != FznsoValueString) {
				return "option `gurobi_dll' expects a string";
			}
			// Deliberately does not load: this is the option that makes a load
			// possible in the first place, so it has to be settable before one
			// has been tried, and it must not be the thing that reports a
			// failure of the default search. Clearing `tried_` is what makes
			// the next attempt use this path and nothing else.
			dll_path_ = std::string{value.as_string()};
			tried_ = false;
			load_error_.clear();
			return std::nullopt;
		}
		if (name == "gurobi_write_model") {
			if (value.kind() != FznsoValueString) {
				return "option `gurobi_write_model' expects a string";
			}
			write_model_ = std::string{value.as_string()};
		} else if (name == "gurobi_abs_gap" || name == "gurobi_rel_gap" ||
		           name == "gurobi_int_tol") {
			if (value.kind() != FznsoValueFloat) {
				return "option `" + std::string{name} + "' expects a float";
			}
			double v = value.as_float();
			if (v < 0.0) {
				return "option `" + std::string{name} + "' must not be negative, got " +
				       std::to_string(v);
			}
			(name == "gurobi_abs_gap" ? abs_gap_ : name == "gurobi_rel_gap" ? rel_gap_ : int_tol_) =
				v;
		} else {
			return "unknown option `" + std::string{name} + "'";
		}
		// The value is kept whatever this says, so that a caller who fixes the
		// installation and sets `gurobi_dll` does not have to set it again.
		// This is one of the two places a missing Gurobi can be reported at
		// all, and the earlier of the two.
		if (!available()) {
			return load_error_;
		}
		return std::nullopt;
	}

	fznso::Value option_get(std::string_view name) const override {
		if (name == "gurobi_dll") {
			return keep(name, fznso::OwnedValue{dll_path_});
		}
		if (name == "gurobi_write_model") {
			return keep(name, fznso::OwnedValue{write_model_});
		}
		if (name == "gurobi_abs_gap") {
			return keep(name, fznso::OwnedValue{abs_gap_});
		}
		if (name == "gurobi_rel_gap") {
			return keep(name, fznso::OwnedValue{rel_gap_});
		}
		if (name == "gurobi_int_tol") {
			return keep(name, fznso::OwnedValue{int_tol_});
		}
		return fznso::Value{};
	}

	fznso::Value statistic(std::string_view name) const override {
		if (name == "nodes") {
			return keep(name, fznso::OwnedValue{nodes_});
		}
		if (name == "open_nodes") {
			return keep(name, fznso::OwnedValue{open_nodes_});
		}
		if (name == "gurobi_mip_gap") {
			return keep(name, fznso::OwnedValue{mip_gap_});
		}
		if (name == "gurobi_simplex_iterations") {
			return keep(name, fznso::OwnedValue{simplex_iterations_});
		}
		if (name == "gurobi_work") {
			return keep(name, fznso::OwnedValue{work_});
		}
		return fznso::Value{};
	}

private:
	static int __stdcall callback(GRBmodel* model, void* cbdata, int where, void* usrdata);

	/// Open the library and the environment, once, and remember why not.
	///
	/// Never called from the constructor: `solver_create` must succeed on a
	/// machine with no Gurobi, and its entry point is `noexcept`, so a throw
	/// there is a terminate rather than an error.
	bool available();

	/// `available()`, plus a model to put things in.
	bool ready() {
		if (!available()) {
			return false;
		}
		if (model_ == nullptr) {
			new_model();
		}
		return true;
	}

	void new_model() {
		call(fn_.GRBnewmodel(env_, &model_, "fznso", 0, nullptr, nullptr, nullptr, nullptr,
		                     nullptr),
		     "unable to create a model");
	}

	/// Delete the last `n`, given the attribute that counts them and the call
	/// that removes them.
	void drop_last(std::size_t n, const char* count_attr,
	               int(__stdcall* remove)(GRBmodel*, int, const int*), const char* what) {
		if (n == 0) {
			return;
		}
		int count = 0;
		call(fn_.GRBgetintattr(model_, count_attr, &count), what);
		auto have = static_cast<std::size_t>(count);
		std::vector<int> doomed(std::min(n, have));
		std::iota(doomed.begin(), doomed.end(), static_cast<int>(have - doomed.size()));
		call(remove(model_, static_cast<int>(doomed.size()), doomed.data()), what);
	}

	std::string message(const char* what) const {
		std::string text = std::string{"Gurobi: "} + what;
		if (env_ != nullptr && fn_.GRBgeterrormsg != nullptr) {
			const char* detail = fn_.GRBgeterrormsg(env_);
			if (detail != nullptr && *detail != '\0') {
				text += ": ";
				text += detail;
			}
		}
		return text;
	}

	void call(int error, const char* what) const {
		if (error != 0) {
			throw std::runtime_error(message(what));
		}
	}

	/// A borrowed value needs its payload to outlive the call, and an earlier
	/// read must survive a later one — hence a node-based container.
	fznso::Value keep(std::string_view name, fznso::OwnedValue value) const {
		auto it = cache_.insert_or_assign(std::string{name}, std::move(value)).first;
		return fznso::Value{it->second};
	}

	Symbols fn_;
	void* dll_ = nullptr;
	GRBenv* env_ = nullptr;
	GRBmodel* model_ = nullptr;

	/// Which of Gurobi's constraint sequences each of the core's rows went to,
	/// by the core's row number; see `truncate`.
	enum RowKind : std::uint8_t { kLinear, kIndicator, kQuadratic };
	std::vector<std::uint8_t> row_kind_;

	/// Whether a load has been attempted, and what it said if it failed. A
	/// second attempt is only made after `gurobi_dll` changes, because the
	/// search is the slow part and its answer does not change on its own.
	bool tried_ = false;
	std::string load_error_;

	std::string dll_path_;
	std::string write_model_;
	double abs_gap_ = -1.0;
	double rel_gap_ = 1e-8;
	double int_tol_ = 1e-8;
	std::int64_t nodes_ = 0;
	std::int64_t open_nodes_ = 0;
	double mip_gap_ = 0.0;
	std::int64_t simplex_iterations_ = 0;
	double work_ = 0.0;
	mutable std::map<std::string, fznso::OwnedValue> cache_;
};

bool GurobiBackend::available() {
	if (env_ != nullptr) {
		return true;
	}
	if (tried_) {
		return false;
	}
	tried_ = true;

	std::vector<std::string> paths =
		dll_path_.empty() ? candidates() : std::vector<std::string>{dll_path_};
	std::string opened;
	for (const std::string& path : paths) {
		dll_ = dll_open(path);
		if (dll_ != nullptr) {
			opened = path;
			break;
		}
	}
	if (dll_ == nullptr) {
		// The list is the message: a user reading which paths were tried fixes
		// their installation in a minute, where "Gurobi not found" is a bug
		// report.
		load_error_ = "unable to load the Gurobi library. Tried";
		for (const std::string& path : paths) {
			load_error_ += " " + path;
		}
		load_error_ += ". Set the `gurobi_dll' option to the library's path.";
		return false;
	}

	const char* missing = nullptr;
	auto resolve = [&](void* slot, const char* name) {
		void* symbol = dll_symbol(dll_, name);
		if (symbol == nullptr && missing == nullptr) {
			missing = name;
		}
		*static_cast<void**>(slot) = symbol;
	};
#define GRB_RESOLVE(sym) resolve(reinterpret_cast<void*>(&fn_.sym), #sym)
	GRB_RESOLVE(GRBstartenv);
	GRB_RESOLVE(GRBfreeenv);
	GRB_RESOLVE(GRBgeterrormsg);
	GRB_RESOLVE(GRBgetenv);
	GRB_RESOLVE(GRBsetintparam);
	GRB_RESOLVE(GRBsetdblparam);
	GRB_RESOLVE(GRBnewmodel);
	GRB_RESOLVE(GRBfreemodel);
	GRB_RESOLVE(GRBupdatemodel);
	GRB_RESOLVE(GRBoptimize);
	GRB_RESOLVE(GRBterminate);
	GRB_RESOLVE(GRBwrite);
	GRB_RESOLVE(GRBaddvars);
	GRB_RESOLVE(GRBaddconstr);
	GRB_RESOLVE(GRBaddgenconstrIndicator);
	GRB_RESOLVE(GRBaddqconstr);
	GRB_RESOLVE(GRBdelvars);
	GRB_RESOLVE(GRBdelconstrs);
	GRB_RESOLVE(GRBdelgenconstrs);
	GRB_RESOLVE(GRBdelqconstrs);
	GRB_RESOLVE(GRBgetintattr);
	GRB_RESOLVE(GRBsetintattr);
	GRB_RESOLVE(GRBgetdblattr);
	GRB_RESOLVE(GRBgetdblattrarray);
	GRB_RESOLVE(GRBsetdblattrlist);
	GRB_RESOLVE(GRBsetobjectiven);
	GRB_RESOLVE(GRBsetcallbackfunc);
	GRB_RESOLVE(GRBcbget);
	// 12.0.0 ships without `GRBemptyenv`; the internal one behind it takes the
	// version it is being called for. Only the absence of both is a failure, so
	// this one is resolved without reporting.
	*reinterpret_cast<void**>(&fn_.GRBemptyenv) = dll_symbol(dll_, "GRBemptyenv");
	if (fn_.GRBemptyenv == nullptr) {
		GRB_RESOLVE(GRBemptyenvinternal);
	}
#undef GRB_RESOLVE
	if (missing != nullptr) {
		load_error_ = "the library at " + opened + " is not a Gurobi library: it has no `" +
		              missing + "' symbol.";
		return false;
	}

	// Created empty and started explicitly, so that logging is off before
	// start-up: `GRBstartenv` prints the licence banner, and this solver's
	// output is a solution stream.
	int error = fn_.GRBemptyenv != nullptr ? fn_.GRBemptyenv(&env_)
	                                       : fn_.GRBemptyenvinternal(&env_, 12, 0, 0);
	if (error != 0) {
		load_error_ = message("unable to create an environment");
		env_ = nullptr;
		return false;
	}
	fn_.GRBsetintparam(env_, "LogToConsole", 0);
	error = fn_.GRBstartenv(env_);
	if (error != 0) {
		// A licence that has expired, is for another machine, or is missing
		// altogether arrives here, and is the same kind of thing as a missing
		// library: nothing this solver can do, and everything the user needs to
		// know is in Gurobi's own message.
		load_error_ = message("unable to start an environment");
		fn_.GRBfreeenv(env_);
		env_ = nullptr;
		return false;
	}
	fn_.GRBsetintparam(env_, "OutputFlag", 0);
	return true;
}

int __stdcall GurobiBackend::callback(GRBmodel* model, void* cbdata, int where, void* usrdata) {
	auto* state = static_cast<CallbackState*>(usrdata);
	const Symbols& fn = *state->fn;
	switch (where) {
	case kCallbackMip: {
		double left = 0.0;
		if (fn.GRBcbget(cbdata, where, kCallbackMipNodesLeft, &left) == 0) {
			state->open_nodes = static_cast<std::int64_t>(left);
		}
		double bound = 0.0;
		if (fn.GRBcbget(cbdata, where, kCallbackMipObjBound, &bound) == 0 &&
		    (!state->have_bound || bound != state->bound)) {
			// This fires constantly and mostly says the same number; only a
			// changed bound is news.
			state->have_bound = true;
			state->bound = bound;
			state->sink->bound(bound);
		}
		break;
	}
	case kCallbackMessage: {
		if (state->log) {
			char* text = nullptr;
			if (fn.GRBcbget(cbdata, where, kCallbackMessageString,
			                reinterpret_cast<void*>(&text)) == 0 &&
			    text != nullptr) {
				state->sink->log(text);
			}
		}
		break;
	}
	case kCallbackMipSol: {
		if (state->intermediate && !state->values.empty()) {
			double objective = 0.0;
			fn.GRBcbget(cbdata, where, kCallbackMipSolObjective, &objective);
			if (fn.GRBcbget(cbdata, where, kCallbackMipSolSolution, state->values.data()) == 0) {
				state->sink->solution(state->values.data(), state->values.size(), objective);
				state->streamed++;
			}
		}
		break;
	}
	default:
		return 0;
	}
	// Polled wherever the callback runs, so that a stop asked for mid-search —
	// including one asked for by the solution just reported — is acted on.
	if (state->sink->should_stop()) {
		fn.GRBterminate(model);
	}
	return 0;
}

RunOutcome GurobiBackend::solve(const MipOptions& options, MipSink& sink) {
	RunOutcome outcome;
	if (!ready()) {
		outcome.status = MipStatus::Error;
		outcome.error = load_error_;
		return outcome;
	}

	GRBenv* env = fn_.GRBgetenv(model_);
	call(fn_.GRBsetintparam(env, "OutputFlag", 0), "unable to switch off the log");
	call(fn_.GRBsetintparam(env, "Threads", static_cast<int>(options.threads)),
	     "unable to set the thread count");
	if (options.time_limit_ms.has_value()) {
		call(fn_.GRBsetdblparam(env, "TimeLimit",
		                        static_cast<double>(*options.time_limit_ms) / 1000.0),
		     "unable to set the time limit");
	}
	if (options.random_seed.has_value()) {
		call(fn_.GRBsetintparam(env, "Seed", static_cast<int>(*options.random_seed)),
		     "unable to set the random seed");
	}
	if (abs_gap_ >= 0.0) {
		call(fn_.GRBsetdblparam(env, "MIPGapAbs", abs_gap_), "unable to set the absolute gap");
	}
	if (rel_gap_ >= 0.0) {
		call(fn_.GRBsetdblparam(env, "MIPGap", rel_gap_), "unable to set the relative gap");
	}
	if (int_tol_ >= 0.0) {
		call(fn_.GRBsetdblparam(env, "IntFeasTol", int_tol_),
		     "unable to set the integrality tolerance");
	}
	// A product of two columns is not convex and Gurobi refuses one unless it is
	// told to solve such a model. Ignored rather than checked, because it does
	// nothing to a model without a product and Gurobi before 9 has no such
	// parameter — where the quadratic rows it guards cannot arise either.
	fn_.GRBsetintparam(env, "NonConvex", 2);
	if (options.all_solutions) {
		// The pool is what has the solutions that tie with the optimum;
		// `PoolGap` 0 is what keeps the ones that do not out of it.
		call(fn_.GRBsetintparam(env, "PoolSearchMode", 2), "unable to enable the solution pool");
		call(fn_.GRBsetintparam(env, "PoolSolutions", kPoolLimit),
		     "unable to size the solution pool");
		call(fn_.GRBsetdblparam(env, "PoolGap", 0.0), "unable to restrict the solution pool");
	}

	call(fn_.GRBupdatemodel(model_), "unable to update the model");
	if (!write_model_.empty()) {
		call(fn_.GRBwrite(model_, write_model_.c_str()), "unable to write the model");
	}

	int columns = 0;
	call(fn_.GRBgetintattr(model_, "NumVars", &columns), "unable to count the columns");

	CallbackState state;
	state.fn = &fn_;
	state.sink = &sink;
	// With the pool on, every solution is reported from it below, so streaming
	// improving ones as well would report some of them twice.
	state.intermediate = options.intermediate && !options.all_solutions;
	state.log = options.verbose && sink.log_wanted();
	state.values.assign(static_cast<std::size_t>(columns), 0.0);
	call(fn_.GRBsetcallbackfunc(model_, &GurobiBackend::callback, &state),
	     "unable to install the callback");

	int error = fn_.GRBoptimize(model_);
	fn_.GRBsetcallbackfunc(model_, nullptr, nullptr);
	outcome.streamed = state.streamed;
	if (error != 0) {
		outcome.status = MipStatus::Error;
		outcome.error = message("unable to solve the model");
		return outcome;
	}

	double nodes = 0.0;
	if (fn_.GRBgetdblattr(model_, "NodeCount", &nodes) == 0) {
		nodes_ = static_cast<std::int64_t>(nodes);
	}
	open_nodes_ = state.open_nodes;
	// Absent on a model Gurobi answered without searching, so a failure to read
	// any of these leaves the previous run's value rather than inventing one.
	double gap = 0.0;
	if (fn_.GRBgetdblattr(model_, "MIPGap", &gap) == 0) {
		mip_gap_ = gap;
	}
	double iterations = 0.0;
	if (fn_.GRBgetdblattr(model_, "IterCount", &iterations) == 0) {
		simplex_iterations_ = static_cast<std::int64_t>(iterations);
	}
	double work = 0.0;
	if (fn_.GRBgetdblattr(model_, "Work", &work) == 0) {
		work_ = work;
	}
	double bound = 0.0;
	// Absent on a model with no objective, and on an LP, so a failure here is
	// not one.
	if (fn_.GRBgetdblattr(model_, "ObjBoundC", &bound) == 0) {
		sink.bound(bound);
	}
	int found = 0;
	if (fn_.GRBgetintattr(model_, "SolCount", &found) != 0) {
		found = 0;
	}

	int status = 0;
	call(fn_.GRBgetintattr(model_, "Status", &status), "unable to read the status");
	switch (status) {
	case kStatusOptimal:
		outcome.status = MipStatus::Optimal;
		break;
	case kStatusInfeasible:
		outcome.status = MipStatus::Infeasible;
		break;
	case kStatusUnbounded:
	case kStatusInfOrUnbounded:
		outcome.status = MipStatus::Unbounded;
		break;
	default:
		// Every remaining status is a limit or an interrupt, which proves
		// nothing either way; what was found before it is still a solution.
		outcome.status = found > 0 ? MipStatus::Feasible : MipStatus::Unknown;
		break;
	}

	const bool solved =
		outcome.status == MipStatus::Optimal || outcome.status == MipStatus::Feasible;
	if (!solved || found == 0) {
		// No columns and no rows: the empty assignment is a solution, and
		// Gurobi counts none.
		outcome.has_solution = outcome.status == MipStatus::Optimal && columns == 0;
		return outcome;
	}
	if (options.all_solutions) {
		std::vector<double> values(static_cast<std::size_t>(columns));
		for (int i = 0; i < found; i++) {
			call(fn_.GRBsetintparam(env, "SolutionNumber", i),
			     "unable to select a pooled solution");
			double objective = 0.0;
			fn_.GRBgetdblattr(model_, "PoolObjVal", &objective);
			if (columns > 0) {
				call(fn_.GRBgetdblattrarray(model_, "Xn", 0, columns, values.data()),
				     "unable to read a pooled solution");
			}
			sink.solution(values.data(), values.size(), objective);
			outcome.streamed++;
			if (sink.should_stop()) {
				break;
			}
		}
		// Reported already; saying so is what stops the core repeating the last.
		outcome.has_solution = true;
		return outcome;
	}
	outcome.values.resize(static_cast<std::size_t>(columns));
	if (columns > 0) {
		call(fn_.GRBgetdblattrarray(model_, "X", 0, columns, outcome.values.data()),
		     "unable to read the solution");
	}
	// Zero on a model with no objective, which is what the core expects there.
	fn_.GRBgetdblattr(model_, "ObjVal", &outcome.objective);
	outcome.has_solution = true;
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
		kGurobiCaps,
		std::vector<FznsoOption>{
			// The counterpart of MiniZinc's `--gurobi-dll`, and the way out of a
			// Gurobi this solver's own search does not find.
			{fznso::str("gurobi_dll"), kStr, fznso::Value{defaults().empty}.raw()},
			{fznso::str("gurobi_write_model"), kStr, fznso::Value{defaults().empty}.raw()},
			{fznso::str("gurobi_abs_gap"), kF, fznso::Value{defaults().abs_gap}.raw()},
			{fznso::str("gurobi_rel_gap"), kF, fznso::Value{defaults().rel_gap}.raw()},
			{fznso::str("gurobi_int_tol"), kF, fznso::Value{defaults().int_tol}.raw()},
		},
		std::vector<FznsoStatistic>{
			// A registry name rather than a `gurobi_` one: the shared list
			// leaves it out because only a backend whose search reports the
			// open node count can answer it, and Gurobi's does — from the
			// callback, since no attribute carries it once the solve is over.
			{fznso::str("open_nodes"), kI, false, true},
			// `{ident, type, from a solution, from the solver}`. All three are
			// counters of the search, so none is meaningful on a solution.
			{fznso::str("gurobi_mip_gap"), kF, false, true},
			{fznso::str("gurobi_simplex_iterations"), kI, false, true},
			{fznso::str("gurobi_work"), kF, false, true},
		}};
	return d;
}

class GurobiSolver final : public MipSolver {
public:
	GurobiSolver() : MipSolver(std::make_unique<GurobiBackend>()) {}

	static FznsoConstraintList constraint_list() { return declarations().constraints(); }
	static FznsoTypeList decision_list() { return declarations().decisions(); }
	static FznsoObjectiveList objective_list() { return declarations().objectives(); }
	static FznsoOptionList option_list() { return declarations().options(); }
	static FznsoStatisticList statistic_list() { return declarations().statistics(); }
};

} // namespace

// The library is `libgurobi.*` — in `lib/fznso/`, which is what keeps it apart
// from the `libgurobi<major>.*` it loads — so the entry points are
// `fznso_gurobi_…`.
FZNSO_EXPORT_SOLVER(GurobiSolver, gurobi);

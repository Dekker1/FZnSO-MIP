#include "mip_backend.hh"

namespace fznso_mip {

// The core only dispatches a constraint the solver declared, and a constraint is
// only declared when the matching capability flag is set — so reaching either of
// these means a backend claimed a capability it does not implement.

void MipBackend::add_indicator_row(int /*bin_col*/, bool /*on_value*/, std::size_t /*nnz*/,
                                   const int* /*idx*/, const double* /*val*/, RowSense /*sense*/,
                                   double /*rhs*/) {
	throw std::logic_error("this backend declared indicator constraints but implements none");
}

void MipBackend::set_lex_objective(std::size_t /*n*/, const int* /*cols*/, bool /*maximise*/) {
	throw std::logic_error("this backend declared lexicographic objectives but implements none");
}

void MipBackend::add_quadratic_row(int /*out_col*/, int /*a_col*/, int /*b_col*/,
                                   double /*rhs*/) {
	throw std::logic_error("this backend declared quadratic constraints but implements none");
}

// Advice, not a capability: a backend that cannot take a starting point is not
// wrong, only slower, so this one is silent rather than loud.
void MipBackend::set_start(std::size_t /*n*/, const int* /*cols*/, const double* /*values*/) {}

void MipBackend::truncate(std::size_t /*first_row*/, std::size_t /*first_col*/) {
	throw std::logic_error("this backend declared incremental building but implements none");
}

} // namespace fznso_mip

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

void MipBackend::add_quadratic_row(int /*out_col*/, int /*a_col*/, int /*b_col*/) {
	throw std::logic_error("this backend declared quadratic constraints but implements none");
}

} // namespace fznso_mip

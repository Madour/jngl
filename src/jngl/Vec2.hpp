// Copyright 2018 Jan Niklas Hasse <jhasse@bixense.com>
// For conditions of distribution and use, see copyright notice in LICENSE.txt

#pragma once

#include <boost/version.hpp>

namespace jngl {

class Vec2 {
public:
	Vec2(double x, double y);

	double x;
	double y;
};

} // namespace jngl

jngl::Vec2 operator/(const jngl::Vec2& lhs, const double v);

#if BOOST_VERSION >= 106200
#include <boost/qvm/vec_traits.hpp>

namespace boost {
namespace qvm {
template <> struct vec_traits<jngl::Vec2> {
	static int const dim = 2;
	typedef double scalar_type;

	template <int I> static inline scalar_type& write_element(jngl::Vec2& v) {
		return (&v.x)[I];
	}
	template <int I> static inline scalar_type read_element(const jngl::Vec2& v) {
		return (&v.x)[I];
	}

	static inline scalar_type& write_element_idx(int i, jngl::Vec2& v) {
		return (&v.x)[i];
	}
	static inline scalar_type read_element_idx(int i, jngl::Vec2 const& v) {
		return (&v.x)[i];
	}
};
} // namespace qvm
} // namespace boost
#endif
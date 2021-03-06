// Copyright Hans Dembinski 2020
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_HISTOGRAM_AXIS_BINARY_HPP
#define BOOST_HISTOGRAM_AXIS_BINARY_HPP

#include <boost/core/nvp.hpp>
#include <boost/histogram/axis/iterator.hpp>
#include <boost/histogram/axis/metadata_base.hpp>
#include <boost/histogram/detail/relaxed_equal.hpp>
#include <boost/histogram/detail/replace_type.hpp>
#include <boost/histogram/fwd.hpp>
#include <string>

namespace boost {
namespace histogram {
namespace axis {

template <class MetaData>
class binary : public iterator_mixin<binary<MetaData>>, public metadata_base_t<MetaData> {
  using value_type = bool;
  using metadata_base = metadata_base_t<MetaData>;
  using metadata_type = typename metadata_base::metadata_type;

public:
  explicit binary(metadata_type meta = {}) : metadata_base(std::move(meta)) {}

  index_type index(value_type x) const noexcept { return static_cast<index_type>(x); }

  value_type value(index_type i) const noexcept { return static_cast<value_type>(i); }

  value_type bin(index_type i) const noexcept { return value(i); }

  index_type size() const noexcept { return 2; }

  static constexpr bool inclusive() noexcept { return true; }

  template <class M>
  bool operator==(const binary<M>& o) const noexcept {
    return detail::relaxed_equal{}(this->metadata(), o.metadata());
  }

  template <class M>
  bool operator!=(const binary<M>& o) const noexcept {
    return !operator==(o);
  }

  template <class Archive>
  void serialize(Archive& ar, unsigned /* version */) {
    ar& make_nvp("meta", this->metadata());
  }

private:
  template <class M>
  friend class binary;
};

#if __cpp_deduction_guides >= 201606

binary()->binary<null_type>;

template <class M>
binary(M)->binary<detail::replace_type<std::decay_t<M>, const char*, std::string>>;

#endif

} // namespace axis
} // namespace histogram
} // namespace boost

#endif // BOOST_HISTOGRAM_AXIS_BINARY_HPP

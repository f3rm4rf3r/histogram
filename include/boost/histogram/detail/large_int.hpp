// Copyright 2018-2019 Hans Dembinski
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt
// or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_HISTOGRAM_DETAIL_LARGE_INT_HPP
#define BOOST_HISTOGRAM_DETAIL_LARGE_INT_HPP

#include <boost/assert.hpp>
#include <boost/histogram/detail/static_if.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace boost {
namespace histogram {
namespace detail {

template <bool B, class T>
struct is_unsigned_integral_impl : std::false_type {};

template <class T>
struct is_unsigned_integral_impl<true, T> : std::is_unsigned<T> {};

template <class T>
using is_unsigned_integral = is_unsigned_integral_impl<std::is_integral<T>::value, T>;

template <class T>
bool safe_increment(T& t) {
  if (t < std::numeric_limits<T>::max()) {
    ++t;
    return true;
  }
  return false;
}

template <class T, class U>
bool safe_radd(T& t, const U& u) {
  static_assert(is_unsigned_integral<T>::value, "T must be unsigned integral type");
  static_assert(is_unsigned_integral<U>::value, "T must be unsigned integral type");
  if (static_cast<T>(std::numeric_limits<T>::max() - t) >= u) {
    t += static_cast<T>(u); // static_cast to suppress conversion warning
    return true;
  }
  return false;
}

// An integer type which can grow arbitrarily large (until memory is exhausted).
// Use boost.multiprecision.cpp_int in your own code, it is much more sophisticated.
// We use it only to reduce coupling between boost libs.
template <class Allocator>
struct large_int {
  explicit large_int(const Allocator& a = {}) : data(1, 0, a) {}
  explicit large_int(std::uint64_t v, const Allocator& a = {}) : data(1, v, a) {}

  large_int(const large_int&) = default;
  large_int& operator=(const large_int&) = default;
  large_int(large_int&&) = default;
  large_int& operator=(large_int&&) = default;

  large_int& operator=(std::uint64_t o) {
    data = decltype(data)(1, o);
    return *this;
  }

  large_int& operator++() {
    BOOST_ASSERT(data.size() > 0u);
    std::size_t i = 0;
    while (!safe_increment(data[i])) {
      data[i] = 0;
      ++i;
      if (i == data.size()) {
        data.push_back(1);
        break;
      }
    }
    return *this;
  }

  large_int& operator+=(const large_int& o) {
    if (this == &o) {
      auto tmp{o};
      return operator+=(tmp);
    }
    bool carry = false;
    std::size_t i = 0;
    for (std::uint64_t oi : o.data) {
      auto& di = maybe_extend(i);
      if (carry) {
        if (safe_increment(oi))
          carry = false;
        else {
          ++i;
          continue;
        }
      }
      if (!safe_radd(di, oi)) {
        add_remainder(di, oi);
        carry = true;
      }
      ++i;
    }
    while (carry) {
      auto& di = maybe_extend(i);
      if (safe_increment(di)) break;
      di = 0;
      ++i;
    }
    return *this;
  }

  large_int& operator+=(std::uint64_t o) {
    BOOST_ASSERT(data.size() > 0u);
    if (safe_radd(data[0], o)) return *this;
    add_remainder(data[0], o);
    // carry the one, data may grow several times
    std::size_t i = 1;
    while (true) {
      auto& di = maybe_extend(i);
      if (safe_increment(di)) break;
      di = 0;
      ++i;
    }
    return *this;
  }

  operator double() const noexcept {
    BOOST_ASSERT(data.size() > 0u);
    double result = static_cast<double>(data[0]);
    std::size_t i = 0;
    while (++i < data.size())
      result += static_cast<double>(data[i]) * std::pow(2.0, i * 64);
    return result;
  }

  // total ordering for large_int, large_int
  bool operator<(const large_int& o) const noexcept {
    BOOST_ASSERT(data.size() > 0u);
    BOOST_ASSERT(o.data.size() > 0u);
    // no leading zeros allowed
    BOOST_ASSERT(data.size() == 1 || data.back() > 0u);
    BOOST_ASSERT(o.data.size() == 1 || o.data.back() > 0u);
    if (data.size() < o.data.size()) return true;
    if (data.size() > o.data.size()) return false;
    auto s = data.size();
    while (s > 0u) {
      --s;
      if (data[s] < o.data[s]) return true;
      if (data[s] > o.data[s]) return false;
    }
    return false; // args are equal
  }

  bool operator==(const large_int& o) const noexcept {
    BOOST_ASSERT(data.size() > 0u);
    BOOST_ASSERT(o.data.size() > 0u);
    // no leading zeros allowed
    BOOST_ASSERT(data.size() == 1 || data.back() > 0u);
    BOOST_ASSERT(o.data.size() == 1 || o.data.back() > 0u);
    if (data.size() != o.data.size()) return false;
    return std::equal(data.begin(), data.end(), o.data.begin());
  }

  // copied from boost/operators.hpp
  friend bool operator>(const large_int& x, const large_int& y) { return y < x; }
  friend bool operator<=(const large_int& x, const large_int& y) { return !(y < x); }
  friend bool operator>=(const large_int& x, const large_int& y) { return !(x < y); }
  friend bool operator!=(const large_int& x, const large_int& y) { return !(x == y); }

  // total ordering for large_int, uint64;  partial ordering for large_int, double
  template <class U>
  bool operator<(const U& o) const noexcept {
    BOOST_ASSERT(data.size() > 0u);
    return static_if<is_unsigned_integral<U>>(
        [this](std::uint64_t o) { return data.size() == 1 && data[0] < o; },
        [this](double o) { return operator double() < o; }, o);
  }

  template <class U>
  bool operator>(const U& o) const noexcept {
    BOOST_ASSERT(data.size() > 0u);
    BOOST_ASSERT(data.size() == 1 || data.back() > 0u); // no leading zeros allowed
    return static_if<is_unsigned_integral<U>>(
        [this](std::uint64_t o) { return data.size() > 1 || data[0] > o; },
        [this](double o) { return operator double() > o; }, o);
  }

  template <class U>
  bool operator==(const U& o) const noexcept {
    BOOST_ASSERT(data.size() > 0u);
    return static_if<is_unsigned_integral<U>>(
        [this](std::uint64_t o) { return data.size() == 1 && data[0] == o; },
        [this](double o) { return operator double() == o; }, o);
  }

  template <class U>
  bool operator!=(const U& x) const noexcept {
    return !operator==(x);
  }

  // adapted copy from boost/operators.hpp
  template <class U>
  friend bool operator<=(const large_int& x, const U& y) {
    if (is_unsigned_integral<U>::value) return !(x > y);
    return (x < y) || (x == y);
  }
  template <class U>
  friend bool operator>=(const large_int& x, const U& y) {
    if (is_unsigned_integral<U>::value) return !(x < y);
    return (x > y) || (x == y);
  }
  template <class U>
  friend bool operator>(const U& x, const large_int& y) {
    return y.operator<(x);
  }
  template <class U>
  friend bool operator<(const U& x, const large_int& y) {
    return y.operator>(x);
  }
  template <class U>
  friend bool operator<=(const U& x, const large_int& y) {
    return y >= x;
  }
  template <class U>
  friend bool operator>=(const U& x, const large_int& y) {
    return y <= x;
  }
  template <class U>
  friend bool operator==(const U& y, const large_int& x) {
    return x.operator==(y);
  }
  template <class U>
  friend bool operator!=(const U& y, const large_int& x) {
    return x.operator!=(y);
  }

  std::uint64_t& maybe_extend(std::size_t i) {
    while (i >= data.size()) data.push_back(0);
    return data[i];
  }

  static void add_remainder(std::uint64_t& d, const std::uint64_t o) noexcept {
    BOOST_ASSERT(d > 0u);
    // in decimal system it would look like this:
    // 8 + 8 = 6 = 8 - (9 - 8) - 1
    // 9 + 1 = 0 = 9 - (9 - 1) - 1
    auto tmp = std::numeric_limits<std::uint64_t>::max();
    tmp -= o;
    --d -= tmp;
  }

  std::vector<std::uint64_t, Allocator> data;
};

} // namespace detail
} // namespace histogram
} // namespace boost

#endif
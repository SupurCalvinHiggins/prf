#pragma once

#include <cassert>

#include "prefetcher/int.hpp"

template <i16 Min, i16 Max>
class saturating_counter {
   public:
    using value_type = i16;

   private:
    value_type m_value;

    inline static value_type sat(const value_type value) noexcept {
        if (value < Min) return Min;
        if (value > Max) return Max;
        return value;
    }

    inline static value_type add_sat(const value_type a,
                                     const value_type b) noexcept {
        assert(Min <= a && a <= Max);
        assert(Min <= b && b <= Max);

        value_type result;
        if (__builtin_add_overflow(a, b, &result)) return a > 0 ? Max : Min;
        return sat(result);
    }

    inline static value_type sub_sat(const value_type a,
                                     const value_type b) noexcept {
        assert(Min <= a && a <= Max);
        assert(Min <= b && b <= Max);
        value_type result;
        if (__builtin_sub_overflow(a, b, &result)) return a > 0 ? Max : Min;
        return sat(result);
    }

   public:
    saturating_counter() noexcept : m_value(Min) {}
    saturating_counter(value_type value) noexcept : m_value(value) {
        assert(sat(m_value) == value);
    }

    [[nodiscard]] operator value_type() const noexcept { return m_value; }
    [[nodiscard]] value_type value() const noexcept { return m_value; }

    [[nodiscard]] static value_type max() noexcept { return Max; }
    [[nodiscard]] static value_type min() noexcept { return Max; }

    saturating_counter& operator+=(value_type value) noexcept {
        m_value = add_sat(m_value, value);
        return *this;
    }

    saturating_counter& operator-=(value_type value) noexcept {
        m_value = sub_sat(m_value, value);
        return *this;
    }

    saturating_counter& operator++() noexcept { return *this += 1; }

    saturating_counter operator++(int) noexcept {
        auto old = *this;
        *this += 1;
        return old;
    }

    saturating_counter& operator--() noexcept { return *this -= 1; }

    saturating_counter operator--(int) noexcept {
        auto old = *this;
        *this -= 1;
        return old;
    }
};

#pragma once

template <typename T>
class Optional {
    T m_value;
    bool m_has_value;

   public:
    Optional(T value) : m_value(value), m_has_value(true) {}
    Optional() : m_has_value(false) {}

    bool has_value() const { return m_has_value; }
    T value() const {
        assert(has_value());
        return m_value;
    }
};

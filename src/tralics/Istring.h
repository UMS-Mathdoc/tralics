#pragma once
#include <string>

class LabelInfo;

class Istring : public std::string {
    bool defined{false};

public:
    Istring() = default;
    Istring(const std::string &s) : std::string(s), defined(true) {} // \todo remove special values

    operator bool() const { return defined; }

    [[nodiscard]] auto labinfo() const -> LabelInfo *;
};

inline Istring hlinee_above, hlinee_width, hlinee_below;

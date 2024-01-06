#ifndef UTIL_H
#define UTIL_H

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <nlohmann/json.hpp>

template <> struct fmt::formatter<nlohmann::json> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const nlohmann::json& val, format_context& ctx) const {
        using enum nlohmann::json::value_t;
        switch (val.type()) {
            case null:            return fmt::format_to(ctx.out(), "(null)");
            case object:
            case array:           return fmt::format_to(ctx.out(), "{}", val.dump());
            case string:          return fmt::format_to(ctx.out(), "{}", val.get<std::string>());
            case boolean:         return fmt::format_to(ctx.out(), "{}", val.get<bool>() ? "true" : "false");
            case number_integer:  return fmt::format_to(ctx.out(), "{}", val.get<nlohmann::json::number_integer_t>());
            case number_unsigned: return fmt::format_to(ctx.out(), "{}", val.get<nlohmann::json::number_unsigned_t>());
            case number_float:    return fmt::format_to(ctx.out(), "{}", val.get<nlohmann::json::number_float_t>());
            case binary:          return fmt::format_to(ctx.out(), "(binary)");
            case discarded:       return fmt::format_to(ctx.out(), "(discarded)");
        }
    }
};

#endif /* UTIL_H */

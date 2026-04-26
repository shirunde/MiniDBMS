#include "Schema.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace {

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

} // namespace

std::optional<SqlType> tryParseSqlTypeKeyword(const std::string& word) {
    std::string u = upper(trim(word));
    if (u == "INT" || u == "INTEGER" || u == "BIGINT" || u == "SMALLINT" || u == "TINYINT") {
        return SqlType::Int;
    }
    if (u == "FLOAT" || u == "REAL" || u == "DOUBLE") {
        return SqlType::Float;
    }
    if (u == "TEXT" || u == "VARCHAR" || u == "STRING" || u == "CHAR") {
        return SqlType::Text;
    }
    return std::nullopt;
}

std::optional<Cell> parseLiteralToCell(const std::string& raw, SqlType t) {
    std::string s = trim(raw);
    try {
        if (t == SqlType::Int) {
            size_t idx = 0;
            std::int64_t v = static_cast<std::int64_t>(std::stoll(s, &idx, 10));
            if (idx != s.size()) {
                return std::nullopt;
            }
            return Cell{v};
        }
        if (t == SqlType::Float) {
            size_t idx = 0;
            double v = std::stod(s, &idx);
            if (idx != s.size()) {
                return std::nullopt;
            }
            return Cell{v};
        }
        return Cell{s};
    } catch (...) {
        return std::nullopt;
    }
}

std::string cellToString(const Cell& c) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream os;
                os << v;
                return os.str();
            } else {
                return v;
            }
        },
        c);
}

std::string sqlTypeToString(SqlType t) {
    switch (t) {
    case SqlType::Int:
        return "INT";
    case SqlType::Float:
        return "FLOAT";
    case SqlType::Text:
        return "TEXT";
    }
    return "TEXT";
}

bool cellEqualsTyped(const Cell& a, const Cell& b, SqlType t) {
    if (t == SqlType::Int) {
        const auto* pa = std::get_if<std::int64_t>(&a);
        const auto* pb = std::get_if<std::int64_t>(&b);
        if (!pa || !pb) {
            return false;
        }
        return *pa == *pb;
    }
    if (t == SqlType::Float) {
        const auto* pa = std::get_if<double>(&a);
        const auto* pb = std::get_if<double>(&b);
        if (!pa || !pb) {
            return false;
        }
        return *pa == *pb || (std::isnan(*pa) && std::isnan(*pb));
    }
    const auto* pa = std::get_if<std::string>(&a);
    const auto* pb = std::get_if<std::string>(&b);
    if (!pa || !pb) {
        return false;
    }
    return *pa == *pb;
}

#ifndef SCHEMA_H
#define SCHEMA_H

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

/** 列类型：存储层与 SQL 语义统一 */
enum class SqlType { Int, Float, Text };

struct ColumnDef {
    std::string name;
    SqlType type = SqlType::Text;
};

/** 单元格：与列类型一一对应 */
using Cell = std::variant<std::int64_t, double, std::string>;

/** 识别 INT/FLOAT/TEXT 等；无法识别时返回 nullopt */
std::optional<SqlType> tryParseSqlTypeKeyword(const std::string& word);

/** 将字面量按列类型转为 Cell；失败返回 nullopt */
std::optional<Cell> parseLiteralToCell(const std::string& raw, SqlType t);

std::string cellToString(const Cell& c);

/** 同类型相等比较（用于 WHERE） */
bool cellEqualsTyped(const Cell& a, const Cell& b, SqlType t);

std::string sqlTypeToString(SqlType t);

#endif

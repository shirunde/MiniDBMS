#include "Table.h"
#include <algorithm>

Table::Table() {}
Table::Table(const std::string& n) : name(n) {}
Table::Table(const std::string& n, const std::vector<ColumnDef>& schema) : name(n), schema_(schema) {}

const std::string& Table::getName() const { return name; }
const std::vector<ColumnDef>& Table::getSchema() const { return schema_; }

std::vector<std::string> Table::getColumnNames() const {
    std::vector<std::string> out;
    out.reserve(schema_.size());
    for (const auto& c : schema_) {
        out.push_back(c.name);
    }
    return out;
}

void Table::setSchema(const std::vector<ColumnDef>& s) { schema_ = s; }

int Table::columnIndex(const std::string& column) const {
    for (size_t i = 0; i < schema_.size(); ++i) {
        if (schema_[i].name == column) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

SqlType Table::columnType(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(schema_.size())) {
        return SqlType::Text;
    }
    return schema_[idx].type;
}

void Table::insert(const Record& r) { records.push_back(r); }

static bool rowMatchesWhere(const Record& r, int idx, SqlType colType, const std::string& whereRaw) {
    if (idx < 0 || idx >= static_cast<int>(r.cells.size())) {
        return false;
    }
    auto rhs = parseLiteralToCell(whereRaw, colType);
    if (!rhs.has_value()) {
        return false;
    }
    return cellEqualsTyped(r.cells[idx], *rhs, colType);
}

std::vector<Record> Table::select(const std::string& whereColumn, const std::string& whereValue) const {
    if (whereColumn.empty()) {
        return records;
    }
    std::vector<Record> result;
    int idx = columnIndex(whereColumn);
    if (idx < 0) {
        return result;
    }
    SqlType t = columnType(idx);
    for (const auto& r : records) {
        if (rowMatchesWhere(r, idx, t, whereValue)) {
            result.push_back(r);
        }
    }
    return result;
}

int Table::deleteRows(const std::string& whereColumn, const std::string& whereValue) {
    if (whereColumn.empty()) {
        int count = static_cast<int>(records.size());
        records.clear();
        return count;
    }
    int idx = columnIndex(whereColumn);
    if (idx < 0) {
        return 0;
    }
    SqlType t = columnType(idx);
    size_t before = records.size();
    records.erase(std::remove_if(records.begin(), records.end(), [&](const Record& r) {
        return rowMatchesWhere(r, idx, t, whereValue);
    }), records.end());
    return static_cast<int>(before - records.size());
}

int Table::updateRows(const std::string& updateColumn, const std::string& updateValue, const std::string& whereColumn,
                      const std::string& whereValue) {
    int updateIdx = columnIndex(updateColumn);
    if (updateIdx < 0) {
        return 0;
    }
    SqlType updateType = columnType(updateIdx);
    auto newCell = parseLiteralToCell(updateValue, updateType);
    if (!newCell.has_value()) {
        return 0;
    }
    int whereIdx = whereColumn.empty() ? -1 : columnIndex(whereColumn);
    SqlType whereType = whereIdx >= 0 ? columnType(whereIdx) : SqlType::Text;
    int count = 0;
    for (auto& r : records) {
        bool match = true;
        if (whereIdx >= 0) {
            match = rowMatchesWhere(r, whereIdx, whereType, whereValue);
        }
        if (match && updateIdx < static_cast<int>(r.cells.size())) {
            r.cells[updateIdx] = *newCell;
            ++count;
        }
    }
    return count;
}

std::vector<Record>& Table::getRecords() { return records; }
const std::vector<Record>& Table::getRecords() const { return records; }

void Table::addColumn(const std::string& columnName, SqlType type) {
    if (columnIndex(columnName) >= 0) {
        return;
    }
    schema_.push_back({columnName, type});
    for (auto& r : records) {
        switch (type) {
        case SqlType::Int:
            r.cells.push_back(Cell{static_cast<std::int64_t>(0)});
            break;
        case SqlType::Float:
            r.cells.push_back(Cell{0.0});
            break;
        case SqlType::Text:
            r.cells.push_back(Cell{std::string{}});
            break;
        }
    }
}

void Table::dropColumn(const std::string& columnName) {
    int idx = columnIndex(columnName);
    if (idx < 0) {
        return;
    }
    schema_.erase(schema_.begin() + idx);
    for (auto& r : records) {
        if (idx < static_cast<int>(r.cells.size())) {
            r.cells.erase(r.cells.begin() + idx);
        }
    }
}

bool Table::modifyColumn(const std::string& columnName, SqlType newType) {
    int idx = columnIndex(columnName);
    if (idx < 0) {
        return false;
    }
    SqlType oldType = schema_[static_cast<size_t>(idx)].type;
    if (oldType == newType) {
        return true;
    }
    for (auto& r : records) {
        if (idx < static_cast<int>(r.cells.size())) {
            auto& cell = r.cells[static_cast<size_t>(idx)];
            if (newType == SqlType::Int) {
                double v = 0.0;
                if (std::holds_alternative<std::int64_t>(cell)) {
                    v = static_cast<double>(std::get<std::int64_t>(cell));
                } else if (std::holds_alternative<double>(cell)) {
                    v = std::get<double>(cell);
                } else if (std::holds_alternative<std::string>(cell)) {
                    try {
                        v = std::stod(std::get<std::string>(cell));
                    } catch (...) {
                        v = 0.0;
                    }
                }
                cell = Cell{static_cast<std::int64_t>(v)};
            } else if (newType == SqlType::Float) {
                double v = 0.0;
                if (std::holds_alternative<std::int64_t>(cell)) {
                    v = static_cast<double>(std::get<std::int64_t>(cell));
                } else if (std::holds_alternative<double>(cell)) {
                    v = std::get<double>(cell);
                } else if (std::holds_alternative<std::string>(cell)) {
                    try {
                        v = std::stod(std::get<std::string>(cell));
                    } catch (...) {
                        v = 0.0;
                    }
                }
                cell = Cell{v};
            } else {
                cell = Cell{cellToString(cell)};
            }
        }
    }
    schema_[static_cast<size_t>(idx)].type = newType;
    return true;
}

#include "Executor.h"
#include "Ast.h"
#include "Schema.h"
#include <iostream>
#include <type_traits>
#include <utility>

namespace {

static bool buildInsertRecord(Table& t, const ast::InsertStmt& ins, Record& out, std::string& err) {
    const auto& sch = t.getSchema();
    if (sch.empty()) {
        err = "表无列定义";
        return false;
    }
    out.cells.resize(sch.size());
    for (size_t i = 0; i < sch.size(); ++i) {
        switch (sch[i].type) {
        case SqlType::Int:
            out.cells[i] = Cell{static_cast<std::int64_t>(0)};
            break;
        case SqlType::Float:
            out.cells[i] = Cell{0.0};
            break;
        case SqlType::Text:
            out.cells[i] = Cell{std::string{}};
            break;
        }
    }
    if (!ins.columns.empty()) {
        for (size_t i = 0; i < ins.columns.size() && i < ins.values.size(); ++i) {
            int idx = t.columnIndex(ins.columns[i]);
            if (idx < 0) {
                continue;
            }
            auto c = parseLiteralToCell(ins.values[i], sch[static_cast<size_t>(idx)].type);
            if (!c.has_value()) {
                err = "值与列类型不匹配: " + ins.columns[i];
                return false;
            }
            out.cells[static_cast<size_t>(idx)] = *c;
        }
        return true;
    }
    if (ins.values.size() != sch.size()) {
        err = "列数与值数量不一致";
        return false;
    }
    for (size_t i = 0; i < sch.size(); ++i) {
        auto c = parseLiteralToCell(ins.values[i], sch[i].type);
        if (!c.has_value()) {
            err = "值与列类型不匹配";
            return false;
        }
        out.cells[i] = *c;
    }
    return true;
}

} // namespace

Executor::Executor() {
    FileManager::ensureDatabase(currentDb);
}

bool Executor::ensureConnected() {
    return FileManager::ensureDatabase(currentDb);
}

Table& Executor::loadTable(const std::string& tableName) {
    auto it = tables.find(tableName);
    if (it != tables.end()) {
        return it->second;
    }
    tables[tableName] = FileManager::load(currentDb, tableName);
    return tables[tableName];
}

void Executor::printRows(const Table& table, const std::vector<Record>& rows, const std::vector<std::string>& selectedColumns) {
    std::vector<int> idxs;
    std::vector<std::string> headers;
    std::vector<std::string> allNames = table.getColumnNames();

    if (selectedColumns.empty()) {
        headers = allNames;
        for (size_t i = 0; i < headers.size(); ++i) {
            idxs.push_back(static_cast<int>(i));
        }
    } else {
        for (const auto& c : selectedColumns) {
            int idx = table.columnIndex(c);
            if (idx >= 0) {
                idxs.push_back(idx);
                headers.push_back(c);
            }
        }
    }

    for (size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) {
            std::cout << " | ";
        }
        std::cout << headers[i];
    }
    std::cout << "\n";

    for (const auto& r : rows) {
        for (size_t i = 0; i < idxs.size(); ++i) {
            if (i > 0) {
                std::cout << " | ";
            }
            int idx = idxs[i];
            if (idx >= 0 && idx < static_cast<int>(r.cells.size())) {
                std::cout << cellToString(r.cells[static_cast<size_t>(idx)]);
            }
        }
        std::cout << "\n";
    }
    std::cout << "(" << rows.size() << " rows)\n";
}

void Executor::execute(const ParseResult& pr) {
    if (!pr.errorMsg.empty()) {
        std::cout << "SQL 错误: " << pr.errorMsg << "\n";
        return;
    }
    if (!pr.stmt.has_value()) {
        return;
    }
    if (!ensureConnected()) {
        std::cout << "Cannot connect db.\n";
        return;
    }

    std::visit(
        [this](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, ast::ConnectStmt>) {
                const auto& s = arg;
                currentDb = s.database.empty() ? "default" : s.database;
                tables.clear();
                if (FileManager::ensureDatabase(currentDb)) {
                    std::cout << "Connected to db: " << currentDb << "\n";
                } else {
                    std::cout << "Connect failed.\n";
                }
            } else if constexpr (std::is_same_v<T, ast::CreateDatabaseStmt>) {
                const auto& s = arg;
                if (FileManager::ensureDatabase(s.database)) {
                    std::cout << "Database created: " << s.database << "\n";
                } else {
                    std::cout << "Create database failed.\n";
                }
            } else if constexpr (std::is_same_v<T, ast::DropDatabaseStmt>) {
                const auto& s = arg;
                if (FileManager::dropDatabase(s.database)) {
                    std::cout << "Database dropped: " << s.database << "\n";
                    if (currentDb == s.database) {
                        currentDb = "default";
                        tables.clear();
                        FileManager::ensureDatabase(currentDb);
                    }
                } else {
                    std::cout << "Drop database failed.\n";
                }
            } else if constexpr (std::is_same_v<T, ast::ShowTablesStmt>) {
                (void)arg;
                auto names = FileManager::listTables(currentDb);
                std::cout << "Tables_in_" << currentDb << "\n";
                for (const auto& name : names) {
                    std::cout << name << "\n";
                }
                std::cout << "(" << names.size() << " tables)\n";
            } else if constexpr (std::is_same_v<T, ast::ShowDatabasesStmt>) {
                (void)arg;
                auto names = FileManager::listDatabases();
                std::cout << "Databases\n";
                for (const auto& name : names) {
                    std::cout << name << "\n";
                }
                std::cout << "(" << names.size() << " databases)\n";
            } else if constexpr (std::is_same_v<T, ast::DescribeStmt>) {
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                std::cout << "Field\tType\n";
                for (const auto& c : t.getSchema()) {
                    std::cout << c.name << "\t" << sqlTypeToString(c.type) << "\n";
                }
                std::cout << "(" << t.getSchema().size() << " columns)\n";
            } else if constexpr (std::is_same_v<T, ast::DropTableStmt>) {
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                if (FileManager::dropTable(currentDb, s.table)) {
                    tables.erase(s.table);
                    std::cout << "Table dropped: " << s.table << "\n";
                } else {
                    std::cout << "Drop table failed: " << s.table << "\n";
                }
            } else if constexpr (std::is_same_v<T, ast::AlterTableStmt>) {
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                bool success = false;
                switch (s.op) {
                case ast::AlterOperation::AddColumn:
                    t.addColumn(s.columnName, s.columnType);
                    success = true;
                    std::cout << "Column added: " << s.columnName << "\n";
                    break;
                case ast::AlterOperation::DropColumn:
                    t.dropColumn(s.columnName);
                    success = true;
                    std::cout << "Column dropped: " << s.columnName << "\n";
                    break;
                case ast::AlterOperation::ModifyColumn:
                    success = t.modifyColumn(s.columnName, s.columnType);
                    if (success) {
                        std::cout << "Column modified: " << s.columnName << "\n";
                    } else {
                        std::cout << "Column not found: " << s.columnName << "\n";
                    }
                    break;
                }
                if (success) {
                    FileManager::save(currentDb, t);
                }
            } else if constexpr (std::is_same_v<T, ast::CreateTableStmt>) {
                const auto& s = arg;
                if (s.columns.empty()) {
                    std::cout << "CREATE TABLE requires columns.\n";
                    return;
                }
                Table tab(s.table, s.columns);
                tables[s.table] = std::move(tab);
                FileManager::save(currentDb, tables[s.table]);
                std::cout << "Table created: " << s.table << "\n";
            } else if constexpr (std::is_same_v<T, ast::InsertStmt>) {
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                Record r;
                std::string err;
                if (!buildInsertRecord(t, s, r, err)) {
                    std::cout << err << "\n";
                    return;
                }
                t.insert(r);
                FileManager::save(currentDb, t);
                std::cout << "1 row inserted.\n";
            } else if constexpr (std::is_same_v<T, ast::SelectStmt>) {
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                auto rows = t.select(s.whereColumn, s.whereValue);
                printRows(t, rows, s.selectAll ? std::vector<std::string>{} : s.columns);
            } else if constexpr (std::is_same_v<T, ast::DeleteStmt>) {
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                int n = t.deleteRows(s.whereColumn, s.whereValue);
                FileManager::save(currentDb, t);
                std::cout << n << " rows deleted.\n";
            } else if constexpr (std::is_same_v<T, ast::UpdateStmt>) {
                const auto& s = arg;
                if (!FileManager::tableExists(currentDb, s.table)) {
                    std::cout << "Table not found: " << s.table << "\n";
                    return;
                }
                Table& t = loadTable(s.table);
                int n = t.updateRows(s.setColumn, s.setValue, s.whereColumn, s.whereValue);
                FileManager::save(currentDb, t);
                std::cout << n << " rows updated.\n";
            }
        },
        *pr.stmt);
}

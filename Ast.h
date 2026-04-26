#ifndef AST_H
#define AST_H

#include "Schema.h"
#include <string>
#include <variant>
#include <vector>

namespace ast {

struct ConnectStmt {
    std::string database;
};

struct CreateDatabaseStmt {
    std::string database;
};

struct DropDatabaseStmt {
    std::string database;
};

struct ShowDatabasesStmt {};

struct ShowTablesStmt {};

struct DescribeStmt {
    std::string table;
};

struct DropTableStmt {
    std::string table;
};

enum class AlterOperation { AddColumn, DropColumn, ModifyColumn };

struct AlterTableStmt {
    std::string table;
    AlterOperation op;
    std::string columnName;
    SqlType columnType;
};

/** CREATE TABLE t ( id INT, name TEXT, score FLOAT ); */
struct CreateTableStmt {
    std::string table;
    std::vector<ColumnDef> columns;
};

struct InsertStmt {
    std::string table;
    std::vector<std::string> columns;
    std::vector<std::string> values;
};

struct SelectStmt {
    bool selectAll = false;
    std::vector<std::string> columns;
    std::string table;
    std::string whereColumn;
    std::string whereValue;
};

struct DeleteStmt {
    std::string table;
    std::string whereColumn;
    std::string whereValue;
};

struct UpdateStmt {
    std::string table;
    std::string setColumn;
    std::string setValue;
    std::string whereColumn;
    std::string whereValue;
};

using Stmt = std::variant<ConnectStmt, CreateDatabaseStmt, DropDatabaseStmt, ShowDatabasesStmt, ShowTablesStmt, DescribeStmt,
                            DropTableStmt, AlterTableStmt, CreateTableStmt, InsertStmt, SelectStmt, DeleteStmt, UpdateStmt>;

} // namespace ast

#endif

#ifndef TABLE_H
#define TABLE_H

#include "Record.h"
#include <string>
#include <vector>

class Table {
private:
    std::string name;
    std::vector<ColumnDef> schema_;
    std::vector<Record> records;

public:
    Table();
    explicit Table(const std::string& name);
    Table(const std::string& name, const std::vector<ColumnDef>& schema);

    const std::string& getName() const;
    const std::vector<ColumnDef>& getSchema() const;
    /** 仅列名，兼容旧打印逻辑 */
    std::vector<std::string> getColumnNames() const;
    void setSchema(const std::vector<ColumnDef>& s);
    int columnIndex(const std::string& column) const;
    SqlType columnType(int idx) const;

    void insert(const Record& r);
    std::vector<Record> select(const std::string& whereColumn = "", const std::string& whereValue = "") const;
    int deleteRows(const std::string& whereColumn = "", const std::string& whereValue = "");
    int updateRows(const std::string& updateColumn, const std::string& updateValue, const std::string& whereColumn = "",
                   const std::string& whereValue = "");

    std::vector<Record>& getRecords();
    const std::vector<Record>& getRecords() const;

void addColumn(const std::string& columnName, SqlType type);
void dropColumn(const std::string& columnName);
bool modifyColumn(const std::string& columnName, SqlType newType);
};

#endif

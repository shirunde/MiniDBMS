#include "MiniDB.h"
#include "Executor.h"
#include "Parser.h"

Parser parser;
Executor executor;

MiniDB::MiniDB() {}

void MiniDB::execute(const std::string& sql) {
    ParseResult pr = parser.parse(sql);
    executor.execute(pr);
}
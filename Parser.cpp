#include "Parser.h"
#include "Schema.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace {

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

bool eqKw(const std::string& a, const char* b) {
    return upper(a) == upper(std::string(b));
}

// 词法分析：单引号字符串（'' 表示一个 '）、反引号/双引号标识符、数字、运算符。
bool lexSql(const std::string& sql, std::vector<Token>& out, std::string& err) {
    size_t i = 0;
    const size_t n = sql.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(sql[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
            continue;
        }
        if (c == '\'') {
            ++i;
            std::string s;
            while (i < n) {
                if (sql[i] == '\'') {
                    if (i + 1 < n && sql[i + 1] == '\'') {
                        s += '\'';
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                s += sql[i++];
            }
            out.push_back({Token::STRING, s});
            continue;
        }
        if (c == '`') {
            ++i;
            std::string s;
            while (i < n && sql[i] != '`') {
                s += sql[i++];
            }
            if (i >= n) {
                err = "未闭合的反引号标识符";
                return false;
            }
            ++i;
            out.push_back({Token::WORD, s});
            continue;
        }
        if (c == '"') {
            ++i;
            std::string s;
            while (i < n && sql[i] != '"') {
                s += sql[i++];
            }
            if (i >= n) {
                err = "未闭合的双引号标识符";
                return false;
            }
            ++i;
            out.push_back({Token::WORD, s});
            continue;
        }
        if (c == '=') {
            out.push_back({Token::EQ, ""});
            ++i;
            continue;
        }
        if (c == ',') {
            out.push_back({Token::COMMA, ""});
            ++i;
            continue;
        }
        if (c == '(') {
            out.push_back({Token::LPAREN, ""});
            ++i;
            continue;
        }
        if (c == ')') {
            out.push_back({Token::RPAREN, ""});
            ++i;
            continue;
        }
        if (c == ';') {
            out.push_back({Token::SEMICOLON, ""});
            ++i;
            continue;
        }
        if (c == '*') {
            out.push_back({Token::WORD, "*"});
            ++i;
            continue;
        }
        if (c == '-' && i + 1 < n &&
            (std::isdigit(static_cast<unsigned char>(sql[i + 1])) ||
             (sql[i + 1] == '.' && i + 2 < n && std::isdigit(static_cast<unsigned char>(sql[i + 2]))))) {
            std::string s;
            s += '-';
            ++i;
            bool dot = false;
            while (i < n) {
                unsigned char ch = static_cast<unsigned char>(sql[i]);
                if (std::isdigit(ch)) {
                    s += sql[i++];
                } else if (ch == '.' && !dot) {
                    dot = true;
                    s += sql[i++];
                } else {
                    break;
                }
            }
            out.push_back({Token::NUMBER, s});
            continue;
        }
        if (std::isdigit(c) ||
            (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(sql[i + 1])))) {
            std::string s;
            if (c == '.') {
                s += sql[i++];
            } else {
                while (i < n && std::isdigit(static_cast<unsigned char>(sql[i]))) {
                    s += sql[i++];
                }
                if (i < n && sql[i] == '.') {
                    s += sql[i++];
                }
            }
            while (i < n && std::isdigit(static_cast<unsigned char>(sql[i]))) {
                s += sql[i++];
            }
            out.push_back({Token::NUMBER, s});
            continue;
        }
        if (std::isalpha(c) || c == '_') {
            std::string s;
            while (i < n) {
                unsigned char c2 = static_cast<unsigned char>(sql[i]);
                if (std::isalnum(c2) || c2 == '_') {
                    s += sql[i++];
                } else {
                    break;
                }
            }
            out.push_back({Token::WORD, s});
            continue;
        }
        err = "无法识别的字符";
        return false;
    }
    out.push_back({Token::END, ""});
    return true;
}

const Token& peek(const std::vector<Token>& t, size_t pos) { return t[pos]; }

void skipSemi(const std::vector<Token>& t, size_t& pos) {
    if (pos < t.size() && peek(t, pos).kind == Token::SEMICOLON) {
        ++pos;
    }
}

std::string atomText(const Token& tok) {
    if (tok.kind == Token::STRING || tok.kind == Token::WORD || tok.kind == Token::NUMBER) {
        return tok.text;
    }
    return "";
}

bool expectWord(const std::vector<Token>& t, size_t& pos, const char* kw) {
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    if (!eqKw(peek(t, pos).text, kw)) {
        return false;
    }
    ++pos;
    return true;
}

// CREATE TABLE name ( col TYPE [, col TYPE ...] );  TYPE: INT FLOAT TEXT 等
bool parseCreateTable(const std::vector<Token>& t, size_t& pos, ast::CreateTableStmt& out) {
    if (!expectWord(t, pos, "CREATE") || !expectWord(t, pos, "TABLE")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.table = peek(t, pos).text;
    ++pos;
    if (pos >= t.size() || peek(t, pos).kind != Token::LPAREN) {
        return false;
    }
    ++pos;
    while (pos < t.size() && peek(t, pos).kind == Token::WORD) {
        ColumnDef col;
        col.name = peek(t, pos).text;
        ++pos;
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
            return false;
        }
        auto ty = tryParseSqlTypeKeyword(peek(t, pos).text);
        if (!ty.has_value()) {
            return false;
        }
        col.type = *ty;
        ++pos;
        out.columns.push_back(std::move(col));
        if (pos < t.size() && peek(t, pos).kind == Token::COMMA) {
            ++pos;
        } else {
            break;
        }
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::RPAREN) {
        return false;
    }
    ++pos;
    if (out.columns.empty()) {
        return false;
    }
    skipSemi(t, pos);
    return true;
}

bool parseInsert(const std::vector<Token>& t, size_t& pos, ast::InsertStmt& out) {
    if (!expectWord(t, pos, "INSERT") || !expectWord(t, pos, "INTO")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.table = peek(t, pos).text;
    ++pos;
    if (pos < t.size() && peek(t, pos).kind == Token::LPAREN) {
        ++pos;
        while (pos < t.size() && peek(t, pos).kind == Token::WORD) {
            out.columns.push_back(peek(t, pos).text);
            ++pos;
            if (peek(t, pos).kind == Token::COMMA) {
                ++pos;
            } else {
                break;
            }
        }
        if (pos >= t.size() || peek(t, pos).kind != Token::RPAREN) {
            return false;
        }
        ++pos;
    }
    if (!expectWord(t, pos, "VALUES")) {
        return false;
    }
    // VALUES (v1, v2) 或 VALUES v1, v2（兼容旧写法无括号）
    if (pos < t.size() && peek(t, pos).kind == Token::LPAREN) {
        ++pos;
        while (pos < t.size()) {
            Token::Kind k = peek(t, pos).kind;
            if (k == Token::STRING || k == Token::WORD || k == Token::NUMBER) {
                out.values.push_back(atomText(peek(t, pos)));
                ++pos;
            } else {
                break;
            }
            if (pos < t.size() && peek(t, pos).kind == Token::COMMA) {
                ++pos;
                continue;
            }
            break;
        }
        if (pos >= t.size() || peek(t, pos).kind != Token::RPAREN) {
            return false;
        }
        ++pos;
    } else {
        // 无括号：VALUES 1, 'a' 或 VALUES 1 Tom（空格分隔，兼容旧版）
        while (pos < t.size() && peek(t, pos).kind != Token::SEMICOLON && peek(t, pos).kind != Token::END) {
            Token::Kind k = peek(t, pos).kind;
            if (k == Token::STRING || k == Token::WORD || k == Token::NUMBER) {
                out.values.push_back(atomText(peek(t, pos)));
                ++pos;
            } else {
                break;
            }
            if (pos < t.size() && peek(t, pos).kind == Token::COMMA) {
                ++pos;
            }
        }
    }
    skipSemi(t, pos);
    return true;
}

bool parseWhere(const std::vector<Token>& t, size_t& pos, std::string& col, std::string& val) {
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD || !eqKw(peek(t, pos).text, "WHERE")) {
        return true;
    }
    ++pos;
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    col = peek(t, pos).text;
    ++pos;
    if (pos < t.size() && peek(t, pos).kind == Token::EQ) {
        ++pos;
    }
    if (pos >= t.size()) {
        return false;
    }
    Token::Kind vk = peek(t, pos).kind;
    if (vk != Token::STRING && vk != Token::WORD && vk != Token::NUMBER) {
        return false;
    }
    val = atomText(peek(t, pos));
    ++pos;
    return true;
}

bool parseSelect(const std::vector<Token>& t, size_t& pos, ast::SelectStmt& out) {
    if (!expectWord(t, pos, "SELECT")) {
        return false;
    }
    if (pos >= t.size()) {
        return false;
    }
    if (peek(t, pos).kind == Token::WORD && peek(t, pos).text == "*") {
        out.selectAll = true;
        ++pos;
    } else {
        while (pos < t.size() && peek(t, pos).kind == Token::WORD && !eqKw(peek(t, pos).text, "FROM")) {
            out.columns.push_back(peek(t, pos).text);
            ++pos;
            if (pos < t.size() && peek(t, pos).kind == Token::COMMA) {
                ++pos;
            } else {
                break;
            }
        }
    }
    if (!expectWord(t, pos, "FROM")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.table = peek(t, pos).text;
    ++pos;
    if (!parseWhere(t, pos, out.whereColumn, out.whereValue)) {
        return false;
    }
    skipSemi(t, pos);
    return true;
}

bool parseDelete(const std::vector<Token>& t, size_t& pos, ast::DeleteStmt& out) {
    if (!expectWord(t, pos, "DELETE") || !expectWord(t, pos, "FROM")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.table = peek(t, pos).text;
    ++pos;
    if (!parseWhere(t, pos, out.whereColumn, out.whereValue)) {
        return false;
    }
    skipSemi(t, pos);
    return true;
}

bool parseUpdate(const std::vector<Token>& t, size_t& pos, ast::UpdateStmt& out) {
    if (!expectWord(t, pos, "UPDATE")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.table = peek(t, pos).text;
    ++pos;
    if (!expectWord(t, pos, "SET")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.setColumn = peek(t, pos).text;
    ++pos;
    if (pos < t.size() && peek(t, pos).kind == Token::EQ) {
        ++pos;
    }
    if (pos >= t.size()) {
        return false;
    }
    Token::Kind vk = peek(t, pos).kind;
    if (vk != Token::STRING && vk != Token::WORD && vk != Token::NUMBER) {
        return false;
    }
    out.setValue = atomText(peek(t, pos));
    ++pos;
    if (!parseWhere(t, pos, out.whereColumn, out.whereValue)) {
        return false;
    }
    skipSemi(t, pos);
    return true;
}

bool parseAlterTable(const std::vector<Token>& t, size_t& pos, ast::AlterTableStmt& out) {
    if (!expectWord(t, pos, "ALTER") || !expectWord(t, pos, "TABLE")) {
        return false;
    }
    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    out.table = peek(t, pos).text;
    ++pos;

    if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
        return false;
    }
    std::string opStr = upper(peek(t, pos).text);
    ++pos;

    if (opStr == "ADD") {
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
            return false;
        }
        out.op = ast::AlterOperation::AddColumn;
        out.columnName = peek(t, pos).text;
        ++pos;
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
            return false;
        }
        auto ty = tryParseSqlTypeKeyword(peek(t, pos).text);
        if (!ty.has_value()) {
            return false;
        }
        out.columnType = *ty;
        ++pos;
    } else if (opStr == "DROP") {
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD || !eqKw(peek(t, pos).text, "COLUMN")) {
            return false;
        }
        ++pos;
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
            return false;
        }
        out.op = ast::AlterOperation::DropColumn;
        out.columnName = peek(t, pos).text;
        ++pos;
    } else if (opStr == "MODIFY") {
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD || !eqKw(peek(t, pos).text, "COLUMN")) {
            return false;
        }
        ++pos;
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
            return false;
        }
        out.op = ast::AlterOperation::ModifyColumn;
        out.columnName = peek(t, pos).text;
        ++pos;
        if (pos >= t.size() || peek(t, pos).kind != Token::WORD) {
            return false;
        }
        auto ty = tryParseSqlTypeKeyword(peek(t, pos).text);
        if (!ty.has_value()) {
            return false;
        }
        out.columnType = *ty;
        ++pos;
    } else {
        return false;
    }

    skipSemi(t, pos);
    return true;
}

} // namespace

ParseResult Parser::parse(const std::string& sql) {
    ParseResult r;

    std::string raw = trim(sql);
    if (raw.empty()) {
        return r;
    }

    std::vector<Token> tokens;
    std::string lexErr;
    if (!lexSql(raw, tokens, lexErr)) {
        r.errorMsg = lexErr;
        return r;
    }

    size_t pos = 0;
    if (peek(tokens, pos).kind == Token::END) {
        return r;
    }

    if (peek(tokens, pos).kind != Token::WORD) {
        r.errorMsg = "语句应以关键字开头";
        return r;
    }

    std::string kw = upper(peek(tokens, pos).text);

    if (kw == "CONNECT") {
        ++pos;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "CONNECT 需要数据库名";
            return r;
        }
        ast::ConnectStmt c;
        c.database = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(c)};
        return r;
    }

    if (kw == "CREATE" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "DATABASE")) {
        pos += 2;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "CREATE DATABASE 需要数据库名";
            return r;
        }
        ast::CreateDatabaseStmt c;
        c.database = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(c)};
        return r;
    }

    if (kw == "DROP" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "DATABASE")) {
        pos += 2;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "DROP DATABASE 需要数据库名";
            return r;
        }
        ast::DropDatabaseStmt d;
        d.database = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(d)};
        return r;
    }

    if (kw == "SHOW" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "TABLES")) {
        pos += 2;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{ast::ShowTablesStmt{}};
        return r;
    }

    if (kw == "SHOW" && pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
        eqKw(peek(tokens, pos + 1).text, "DATABASES")) {
        pos += 2;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{ast::ShowDatabasesStmt{}};
        return r;
    }

    if (kw == "DESC" || kw == "DESCRIBE") {
        ++pos;
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "DESC 需要表名";
            return r;
        }
        ast::DescribeStmt d;
        d.table = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(d)};
        return r;
    }

    if (kw == "DROP") {
        if (pos + 1 < tokens.size() && peek(tokens, pos + 1).kind == Token::WORD &&
            eqKw(peek(tokens, pos + 1).text, "DATABASE")) {
            return r;
        }
        if (!expectWord(tokens, pos, "DROP") || !expectWord(tokens, pos, "TABLE")) {
            r.errorMsg = "DROP TABLE 语法错误";
            return r;
        }
        if (pos >= tokens.size() || peek(tokens, pos).kind != Token::WORD) {
            r.errorMsg = "DROP TABLE 需要表名";
            return r;
        }
        ast::DropTableStmt d;
        d.table = peek(tokens, pos).text;
        ++pos;
        skipSemi(tokens, pos);
        r.stmt = ast::Stmt{std::move(d)};
        return r;
    }

    if (kw == "ALTER") {
        ast::AlterTableStmt a;
        if (!parseAlterTable(tokens, pos, a)) {
            r.errorMsg = "ALTER TABLE 语法错误（需: ALTER TABLE t ADD|DROP|MODIFY COLUMN name TYPE;）";
            return r;
        }
        r.stmt = ast::Stmt{std::move(a)};
        return r;
    }

    if (kw == "CREATE") {
        ast::CreateTableStmt c;
        if (!parseCreateTable(tokens, pos, c)) {
            r.errorMsg = "CREATE TABLE 语法错误（需: CREATE TABLE t (列名 INT|FLOAT|TEXT, ...);）";
            return r;
        }
        r.stmt = ast::Stmt{std::move(c)};
        return r;
    }

    if (kw == "INSERT") {
        ast::InsertStmt ins;
        if (!parseInsert(tokens, pos, ins)) {
            r.errorMsg = "INSERT 语法错误";
            return r;
        }
        r.stmt = ast::Stmt{std::move(ins)};
        return r;
    }

    if (kw == "SELECT") {
        ast::SelectStmt s;
        if (!parseSelect(tokens, pos, s)) {
            r.errorMsg = "SELECT 语法错误";
            return r;
        }
        r.stmt = ast::Stmt{std::move(s)};
        return r;
    }

    if (kw == "DELETE") {
        ast::DeleteStmt d;
        if (!parseDelete(tokens, pos, d)) {
            r.errorMsg = "DELETE 语法错误";
            return r;
        }
        r.stmt = ast::Stmt{std::move(d)};
        return r;
    }

    if (kw == "UPDATE") {
        ast::UpdateStmt u;
        if (!parseUpdate(tokens, pos, u)) {
            r.errorMsg = "UPDATE 语法错误";
            return r;
        }
        r.stmt = ast::Stmt{std::move(u)};
        return r;
    }

    r.errorMsg = "不支持的关键字或语句";
    return r;
}

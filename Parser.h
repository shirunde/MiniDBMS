#ifndef PARSER_H
#define PARSER_H

#include "Ast.h"
#include <optional>
#include <string>
#include <vector>

// 词法单元：参考 MySQL 风格——WORD（关键字/标识符）、STRING（单引号，支持 '' 转义）、
// 反引号/双引号在词法阶段已展开为 WORD 文本。
struct Token {
    enum Kind { WORD, STRING, NUMBER, EQ, COMMA, LPAREN, RPAREN, SEMICOLON, END } kind;
    std::string text;
};

/** 解析结果：成功时 stmt 有值；失败时 errorMsg 非空 */
struct ParseResult {
    std::optional<ast::Stmt> stmt;
    std::string errorMsg;
};

class Parser {
public:
    ParseResult parse(const std::string& sql);
};

#endif

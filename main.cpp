#include "MiniDB.h"
#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

/** Windows: console uses GBK by default; source strings are UTF-8 (/utf-8). */
static void setupConsoleUtf8() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
}

int main() {
    setupConsoleUtf8();
    MiniDB db;
    std::cout << "MiniDB started. Type SQL and end with ';'.\n";
    std::cout << "Use EXIT; to quit.\n";

    std::string line;
    // 简易 REPL：每输入一行 SQL，就立即解析并执行。
    while (true) {
        std::cout << "miniDB> ";
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (line == "EXIT;" || line == "exit;" || line == "quit;" || line == "QUIT;") {
            break;
        }
        db.execute(line);
    }
    return 0;
}
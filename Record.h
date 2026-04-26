#ifndef RECORD_H
#define RECORD_H

#include "Schema.h"
#include <vector>

struct Record {
    std::vector<Cell> cells;
};

#endif

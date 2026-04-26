#include "FileManager.h"
#include "Schema.h"
#include <cerrno>
#include <variant>
#include <cstdint>
#include <cstring>
#include <direct.h>
#include <fstream>
#include <io.h>
#include <sstream>
#include <vector>

namespace {

bool removeDirectoryRecursive(const std::string& path) {
    std::string pattern = path + "/*";
    _finddata_t fileInfo;
    intptr_t handle = _findfirst(pattern.c_str(), &fileInfo);
    if (handle == -1) {
        return false;
    }

    bool success = true;
    do {
        std::string name = fileInfo.name;
        if (name == "." || name == "..") {
            continue;
        }
        std::string fullPath = path + "/" + name;
        if (fileInfo.attrib & _A_SUBDIR) {
            if (!removeDirectoryRecursive(fullPath)) {
                success = false;
            }
        } else {
            if (std::remove(fullPath.c_str()) != 0) {
                success = false;
            }
        }
    } while (_findnext(handle, &fileInfo) == 0);

    _findclose(handle);

    if (success && _rmdir(path.c_str()) != 0) {
        return false;
    }
    return success;
}

std::string dbDir(const std::string& dbName) {
    return "data_" + dbName;
}

std::string tableFile(const std::string& dbName, const std::string& tableName) {
    return dbDir(dbName) + "/" + tableName + ".tbl";
}

static const unsigned char kMagicV1[8] = {'M', 'I', 'N', 'I', 'D', 'B', 0x01, 0x00};
static const unsigned char kMagicV2[8] = {'M', 'I', 'N', 'I', 'D', 'B', 0x02, 0x00};

static void writeU32(std::ostream& o, std::uint32_t v) {
    o.put(static_cast<char>(v & 0xFF));
    o.put(static_cast<char>((v >> 8) & 0xFF));
    o.put(static_cast<char>((v >> 16) & 0xFF));
    o.put(static_cast<char>((v >> 24) & 0xFF));
}

static std::uint32_t readU32(std::istream& in) {
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    if (in.gcount() != 4) {
        return 0;
    }
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
}

static void writeString(std::ostream& o, const std::string& s) {
    std::uint32_t len = static_cast<std::uint32_t>(s.size());
    writeU32(o, len);
    if (len > 0) {
        o.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
}

static std::string readString(std::istream& in) {
    std::uint32_t len = readU32(in);
    if (len == 0) {
        return "";
    }
    std::string s(len, '\0');
    in.read(&s[0], static_cast<std::streamsize>(len));
    if (static_cast<std::uint32_t>(in.gcount()) != len) {
        return "";
    }
    return s;
}

static void writeI64(std::ostream& o, std::int64_t v) {
    for (int i = 0; i < 8; ++i) {
        o.put(static_cast<char>((static_cast<std::uint64_t>(v) >> (8 * i)) & 0xFF));
    }
}

static std::int64_t readI64(std::istream& in) {
    unsigned char b[8];
    in.read(reinterpret_cast<char*>(b), 8);
    if (in.gcount() != 8) {
        return 0;
    }
    std::uint64_t u = 0;
    for (int i = 0; i < 8; ++i) {
        u |= static_cast<std::uint64_t>(b[i]) << (8 * i);
    }
    return static_cast<std::int64_t>(u);
}

static void writeF64(std::ostream& o, double d) {
    static_assert(sizeof(double) == 8, "");
    unsigned char b[8];
    std::memcpy(b, &d, 8);
    o.write(reinterpret_cast<const char*>(b), 8);
}

static double readF64(std::istream& in) {
    unsigned char b[8];
    in.read(reinterpret_cast<char*>(b), 8);
    if (in.gcount() != 8) {
        return 0;
    }
    double d;
    std::memcpy(&d, b, 8);
    return d;
}

static std::uint8_t typeTag(SqlType t) {
    switch (t) {
    case SqlType::Int:
        return 0;
    case SqlType::Float:
        return 1;
    case SqlType::Text:
        return 2;
    }
    return 2;
}

static SqlType tagType(std::uint8_t tag) {
    switch (tag) {
    case 0:
        return SqlType::Int;
    case 1:
        return SqlType::Float;
    default:
        return SqlType::Text;
    }
}

static void writeCell(std::ostream& o, const Cell& c, SqlType t) {
    if (t == SqlType::Int) {
        std::int64_t v = std::get<std::int64_t>(c);
        writeI64(o, v);
    } else if (t == SqlType::Float) {
        double v = std::get<double>(c);
        writeF64(o, v);
    } else {
        const std::string& s = std::get<std::string>(c);
        writeString(o, s);
    }
}

static Cell readCell(std::istream& in, SqlType t) {
    if (t == SqlType::Int) {
        return Cell{readI64(in)};
    }
    if (t == SqlType::Float) {
        return Cell{readF64(in)};
    }
    return Cell{readString(in)};
}

static Table loadLegacyText(std::istream& file, const std::string& tableName) {
    Table t(tableName);
    std::string line;
    bool firstLine = true;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() && firstLine) {
            continue;
        }
        std::stringstream ss(line);
        std::string v;
        std::vector<std::string> parts;
        while (std::getline(ss, v, ',')) {
            parts.push_back(v);
        }
        if (firstLine) {
            firstLine = false;
            if (!parts.empty() && parts[0] == "#schema") {
                std::vector<ColumnDef> schema;
                for (size_t i = 1; i < parts.size(); ++i) {
                    ColumnDef cd;
                    cd.name = parts[i];
                    cd.type = SqlType::Text;
                    schema.push_back(std::move(cd));
                }
                t.setSchema(schema);
                continue;
            }
        }
        Record r;
        for (const auto& p : parts) {
            r.cells.push_back(Cell{p});
        }
        t.insert(r);
    }
    return t;
}

static Table loadBinaryV1(std::istream& in, const std::string& tableName) {
    Table t(tableName);
    std::uint32_t ncols = readU32(in);
    if (ncols > 65536) {
        return t;
    }
    std::vector<ColumnDef> schema;
    schema.reserve(ncols);
    for (std::uint32_t i = 0; i < ncols; ++i) {
        ColumnDef cd;
        cd.name = readString(in);
        cd.type = SqlType::Text;
        schema.push_back(std::move(cd));
    }
    t.setSchema(schema);
    std::uint32_t nrows = readU32(in);
    for (std::uint32_t r = 0; r < nrows; ++r) {
        Record rec;
        for (std::uint32_t i = 0; i < ncols; ++i) {
            rec.cells.push_back(Cell{readString(in)});
        }
        t.insert(rec);
    }
    return t;
}

static Table loadBinaryV2(std::istream& in, const std::string& tableName) {
    Table t(tableName);
    std::uint32_t ncols = readU32(in);
    if (ncols > 65536) {
        return t;
    }
    std::vector<ColumnDef> schema;
    schema.reserve(ncols);
    for (std::uint32_t i = 0; i < ncols; ++i) {
        ColumnDef cd;
        cd.name = readString(in);
        unsigned char tag = 0;
        in.read(reinterpret_cast<char*>(&tag), 1);
        if (in.gcount() != 1) {
            return t;
        }
        cd.type = tagType(tag);
        schema.push_back(std::move(cd));
    }
    t.setSchema(schema);
    std::uint32_t nrows = readU32(in);
    const auto& sch = t.getSchema();
    for (std::uint32_t r = 0; r < nrows; ++r) {
        Record rec;
        for (std::uint32_t i = 0; i < ncols; ++i) {
            rec.cells.push_back(readCell(in, sch[i].type));
        }
        t.insert(rec);
    }
    return t;
}

} // namespace

bool FileManager::ensureDatabase(const std::string& dbName) {
    if (dbName.empty()) {
        return false;
    }
    std::string dir = dbDir(dbName);
    int rc = _mkdir(dir.c_str());
    return rc == 0 || errno == EEXIST;
}

bool FileManager::dropDatabase(const std::string& dbName) {
    if (dbName.empty()) {
        return false;
    }
    std::string dir = dbDir(dbName);
    return removeDirectoryRecursive(dir);
}

std::vector<std::string> FileManager::listDatabases() {
    std::vector<std::string> result;
    std::string pattern = "data_*";
    _finddata_t fileInfo;
    intptr_t handle = _findfirst(pattern.c_str(), &fileInfo);
    if (handle == -1) {
        return result;
    }

    do {
        std::string name = fileInfo.name;
        if (name.size() > 5 && name.substr(0, 5) == "data_") {
            result.push_back(name.substr(5));
        }
    } while (_findnext(handle, &fileInfo) == 0);

    _findclose(handle);
    return result;
}

bool FileManager::tableExists(const std::string& dbName, const std::string& tableName) {
    std::ifstream f(tableFile(dbName, tableName));
    return f.good();
}

std::vector<std::string> FileManager::listTables(const std::string& dbName) {
    std::vector<std::string> result;
    std::string pattern = dbDir(dbName) + "/*.tbl";
    _finddata_t fileInfo;
    intptr_t handle = _findfirst(pattern.c_str(), &fileInfo);
    if (handle == -1) {
        return result;
    }

    do {
        std::string name = fileInfo.name;
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".tbl") {
            result.push_back(name.substr(0, name.size() - 4));
        }
    } while (_findnext(handle, &fileInfo) == 0);

    _findclose(handle);
    return result;
}

bool FileManager::dropTable(const std::string& dbName, const std::string& tableName) {
    std::string path = tableFile(dbName, tableName);
    return std::remove(path.c_str()) == 0;
}

void FileManager::save(const std::string& dbName, const Table& table) {
    ensureDatabase(dbName);
    std::ofstream file(tableFile(dbName, table.getName()), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(kMagicV2), 8);
    const auto& sch = table.getSchema();
    std::uint32_t ncols = static_cast<std::uint32_t>(sch.size());
    writeU32(file, ncols);
    for (const auto& c : sch) {
        writeString(file, c.name);
        unsigned char tag = typeTag(c.type);
        file.put(static_cast<char>(tag));
    }
    const auto& recs = table.getRecords();
    writeU32(file, static_cast<std::uint32_t>(recs.size()));
    for (const auto& r : recs) {
        for (std::uint32_t i = 0; i < ncols; ++i) {
            Cell cell;
            if (i < r.cells.size()) {
                cell = r.cells[i];
            } else {
                switch (sch[i].type) {
                case SqlType::Int:
                    cell = Cell{static_cast<std::int64_t>(0)};
                    break;
                case SqlType::Float:
                    cell = Cell{0.0};
                    break;
                case SqlType::Text:
                    cell = Cell{std::string{}};
                    break;
                }
            }
            if (sch[i].type == SqlType::Text && !std::holds_alternative<std::string>(cell)) {
                cell = Cell{cellToString(cell)};
            }
            writeCell(file, cell, sch[i].type);
        }
    }
}

Table FileManager::load(const std::string& dbName, const std::string& tableName) {
    std::string path = tableFile(dbName, tableName);
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return Table(tableName);
    }

    char head[8];
    file.read(head, 8);
    if (file.gcount() == 8) {
        if (std::memcmp(head, kMagicV2, 8) == 0) {
            return loadBinaryV2(file, tableName);
        }
        if (std::memcmp(head, kMagicV1, 8) == 0) {
            return loadBinaryV1(file, tableName);
        }
    }

    file.clear();
    file.seekg(0);
    return loadLegacyText(file, tableName);
}

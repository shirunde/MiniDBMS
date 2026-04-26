# MiniDB (Simple DBMS)

Minimal DBMS in C++ with typed columns and binary storage.

## 数据类型（SQL）

| SQL 关键字 | 存储类型 |
|------------|----------|
| `INT`, `INTEGER`, `BIGINT`, … | 64 位整数 |
| `FLOAT`, `REAL`, `DOUBLE` | 双精度浮点 |
| `TEXT`, `VARCHAR`, `STRING`, `CHAR` | UTF-8 文本 |

## SQL 示例

```sql
CONNECT demo;
CREATE TABLE student (id INT, name TEXT, score FLOAT);
INSERT INTO student VALUES (1, 'Tom', 93.5);
INSERT INTO student (name, score) VALUES ('Jane', 88);
SELECT * FROM student WHERE id = 1;
UPDATE student SET score = 95.0 WHERE id = 1;
DELETE FROM student WHERE id = 1;
DESC student;
SHOW TABLES;
```

- `CREATE TABLE` 必须带括号，且每一列为 **`列名` + `类型`**。
- 字面量：整数、浮点数（如 `3.14`、`-2`）、单引号字符串；`WHERE` / `SET` 按列类型解析并比较。
- 旧版仅文本的表文件仍可加载（列视为 `TEXT`）；**新写入**使用 **v2 二进制格式**（魔数 `MINIDB` + `0x02`），按类型写入 int64 / float64 / 变长字符串。

## Notes

- 数据目录：`data_<库名>/`，表文件：`*.tbl`。
- `WHERE` 仍为**单列等值**条件；未列出的高级 SQL 未实现。

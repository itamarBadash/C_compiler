CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, score REAL, data BLOB, n INTEGER);
WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < 5000)
INSERT INTO t(name, score, data, n)
SELECT 'name' || x, x * 1.5 / 7.0, randomblob(0), (x * 2654435761) % 1000003 - 500000 FROM c;
CREATE INDEX t_score ON t(score);
CREATE INDEX t_n ON t(n);
SELECT count(*), sum(n), avg(score), min(n), max(n), total(score) FROM t;
SELECT id, name, printf('%.6f', score), n FROM t WHERE n BETWEEN -1000 AND 1000 ORDER BY n, id;
SELECT n % 7 AS k, count(*), sum(n), group_concat(id % 10, '') FROM t GROUP BY k ORDER BY k;
SELECT printf('%e|%g|%.17g|%x|%5.2f|%-8s|%08d', score * 1e9, score, score, id, score, name, n)
  FROM t WHERE id IN (1, 2, 3, 77, 4999);
SELECT id, sum(n) OVER (ORDER BY id ROWS BETWEEN 3 PRECEDING AND CURRENT ROW),
       rank() OVER (PARTITION BY id % 5 ORDER BY n DESC) FROM t WHERE id < 40 ORDER BY id;
CREATE TABLE u AS SELECT id, upper(name) AS up, length(name) AS len, hex(name) AS hx FROM t WHERE id % 97 = 0;
SELECT * FROM u ORDER BY id;
SELECT t.id, u.up FROM t JOIN u ON t.id = u.id + 1 ORDER BY t.id LIMIT 10;
SELECT json_object('id', id, 'score', score, 'arr', json_array(n, name)) FROM t WHERE id < 6;
SELECT json_extract('{"a":[1,2,{"b":3.25e-3}]}', '$.a[2].b'), json_valid('{"x":}');
SELECT 9223372036854775807 + 0, -9223372036854775808, 9223372036854775807 * 2,
       1 / 3, 1.0 / 3, 10 % 3, -10 % 3, 0x7fffffff << 1, 1 << 63, -1 >> 1, ~5, 5 & 3, 5 | 3;
SELECT round(2.5), round(-2.5), round(1.23456789, 4), abs(-9223372036854775807), cast('12abc' AS INTEGER),
       cast(3.99 AS INTEGER), cast(-0.0 AS TEXT), 1e308 * 10, -1e308 * 10, 0.1 + 0.2;
SELECT substr('hello world', 3, 4), instr('hello', 'll'), replace('aaaa', 'a', 'bb'), trim('  x  '),
       lower('ÀB'), unicode('€'), char(8364, 65), quote('it''s'), like('h%o', 'hello'), glob('h*', 'hi');
SELECT date('2024-02-28', '+1 day'), datetime(0, 'unixepoch'), strftime('%Y-%j', '2000-12-31'),
       julianday('2000-01-01 12:00:00');
UPDATE t SET score = score * -1 WHERE id % 1000 = 0;
DELETE FROM t WHERE id % 3 = 0;
SELECT count(*), sum(score) FROM t;
SELECT id, score FROM t ORDER BY score LIMIT 5;
VACUUM;
SELECT quote(zeroblob(4)), typeof(1), typeof(1.0), typeof('x'), typeof(NULL), typeof(x'00');

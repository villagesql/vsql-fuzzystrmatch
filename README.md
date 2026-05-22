# VillageSQL Fuzzy String Match Extension

Port of PostgreSQL's [fuzzystrmatch](https://www.postgresql.org/docs/current/fuzzystrmatch.html) extension for VillageSQL. Provides Soundex, Levenshtein edit distance, Metaphone, and Double Metaphone functions for phonetic string comparison.

## Functions

### Soundex

```sql
vsql_fuzzystrmatch.soundex(text) → text
```

Returns the 4-character Soundex phonetic code. Strings that sound alike in English return the same code.

```sql
SELECT vsql_fuzzystrmatch.soundex('Robert');         -- R163
SELECT vsql_fuzzystrmatch.soundex('Rupert');         -- R163 (same as Robert)
SELECT vsql_fuzzystrmatch.soundex('');               -- '' (empty input)
SELECT vsql_fuzzystrmatch.soundex(NULL);             -- NULL
```

```sql
vsql_fuzzystrmatch.difference(text, text) → int
```

Returns 0–4: the number of Soundex code characters the two strings share. 4 = identical phonetic codes; 0 = no overlap.

```sql
SELECT vsql_fuzzystrmatch.difference('Robert', 'Rupert');   -- 4
SELECT vsql_fuzzystrmatch.difference('Robert', 'Sylvia');   -- 0
```

### Levenshtein Edit Distance

```sql
vsql_fuzzystrmatch.levenshtein(source text, target text) → int
```

Returns the minimum number of single-character edits (insert, delete, substitute) needed to transform `source` into `target`. All operations cost 1.

```sql
vsql_fuzzystrmatch.levenshtein_cost(source text, target text,
    ins_cost int, del_cost int, sub_cost int) → int
```

Same as `levenshtein` with configurable per-operation costs.

```sql
vsql_fuzzystrmatch.levenshtein_less_equal(source text, target text, max_d int) → int
```

Returns the edit distance if ≤ `max_d`; returns `max_d + 1` if the distance exceeds the threshold. More efficient for filtering — the DP short-circuits once the threshold is exceeded.

```sql
vsql_fuzzystrmatch.levenshtein_less_equal_cost(source text, target text,
    ins_cost int, del_cost int, sub_cost int, max_d int) → int
```

Combines custom costs with the early-exit threshold.

```sql
SELECT vsql_fuzzystrmatch.levenshtein('kitten', 'sitting');                     -- 3
SELECT vsql_fuzzystrmatch.levenshtein_cost('abc', 'xyz', 1, 1, 10);            -- 6
SELECT vsql_fuzzystrmatch.levenshtein_less_equal('kitten', 'sitting', 5);      -- 3
SELECT vsql_fuzzystrmatch.levenshtein_less_equal('kitten', 'sitting', 2);      -- 3 (= max_d+1)
SELECT vsql_fuzzystrmatch.levenshtein_less_equal_cost('abc', 'xyz', 1,1,1, 2); -- 3
```

Both input strings must be ≤ 255 characters. Costs must be non-negative and ≤ `INT_MAX`. `max_d` must be non-negative.

### Metaphone

```sql
vsql_fuzzystrmatch.metaphone(source text, max_output_length int) → text
```

Returns the Metaphone phonetic code (Philips 1990), truncated to at most `max_output_length` characters. Handles more English pronunciation rules than Soundex and produces variable-length output.

```sql
SELECT vsql_fuzzystrmatch.metaphone('Robert', 10);     -- RBRT
SELECT vsql_fuzzystrmatch.metaphone('Thompson', 10);   -- 0MPSN  (TH → 0)
SELECT vsql_fuzzystrmatch.metaphone('Robert', 2);      -- RB  (truncated)
SELECT vsql_fuzzystrmatch.metaphone(NULL, 10);         -- NULL
```

`max_output_length` must be between 1 and `INT_MAX`.

### Double Metaphone

```sql
vsql_fuzzystrmatch.dmetaphone(source text) → text
vsql_fuzzystrmatch.dmetaphone_alt(source text) → text
```

Returns the primary (`dmetaphone`) and alternate (`dmetaphone_alt`) Double Metaphone codes (Philips 2000). Double Metaphone handles ambiguous pronunciations from multiple linguistic origins. When no alternate code exists, `dmetaphone_alt` falls back to the primary.

```sql
SELECT vsql_fuzzystrmatch.dmetaphone('Robert');      -- RPRT
SELECT vsql_fuzzystrmatch.dmetaphone_alt('Robert');  -- RPRT (no alternate)
SELECT vsql_fuzzystrmatch.dmetaphone('Schmidt');     -- XMT
SELECT vsql_fuzzystrmatch.dmetaphone_alt('Schmidt'); -- SMT
SELECT vsql_fuzzystrmatch.dmetaphone(NULL);          -- NULL
```

## Prerequisites

- VillageSQL 0.0.4-dev or later
- C++17-capable compiler (GCC 9+, Clang 10+, Apple Clang 12+)
- CMake 3.16+

## Installation

Find your VEB directory:

```sql
SHOW VARIABLES LIKE 'veb_dir';
```

Copy the `.veb` file to that directory, then:

```sql
INSTALL EXTENSION vsql_fuzzystrmatch;
```

### Build from Source

Set `VillageSQL_BUILD_DIR` to your VillageSQL build directory and run:

**Linux:**

```bash
export VillageSQL_BUILD_DIR=$HOME/build/villagesql
./build.sh
cd build && make install
```

**macOS:**

```bash
export VillageSQL_BUILD_DIR=~/build/villagesql
./build.sh
cd build && make install
```

Then install:

```sql
INSTALL EXTENSION vsql_fuzzystrmatch;
```

Verify:

```sql
SELECT vsql_fuzzystrmatch.soundex('Robert');
-- R163
```

## Testing

See [TESTING.md](TESTING.md) for the full test suite instructions.

## Uninstall

```sql
UNINSTALL EXTENSION vsql_fuzzystrmatch;
```

Then remove the expanded cache (replace `/path/to/veb_dir` with your actual `veb_dir` value):

```bash
rm -rf /path/to/veb_dir/_expanded/vsql_fuzzystrmatch
```

## Known Limitations

### Function overloading not supported

PostgreSQL fuzzystrmatch exposes overloaded functions (same name, different arity):

- `levenshtein(text, text)` and `levenshtein(text, text, int, int, int)`
- `levenshtein_less_equal(text, text, int)` and `levenshtein_less_equal(text, text, int, int, int, int)`

VEF registers VDFs by function name alone. Two functions with the same name cannot coexist. This extension uses distinct names for the cost-customizable variants:

| PostgreSQL | This extension |
|---|---|
| `levenshtein(s1, s2)` | `levenshtein(s1, s2)` |
| `levenshtein(s1, s2, ins, del, sub)` | **`levenshtein_cost(s1, s2, ins, del, sub)`** |
| `levenshtein_less_equal(s1, s2, max_d)` | `levenshtein_less_equal(s1, s2, max_d)` |
| `levenshtein_less_equal(s1, s2, ins, del, sub, max_d)` | **`levenshtein_less_equal_cost(s1, s2, ins, del, sub, max_d)`** |

To remove this limitation, VEF would need to support arity-based dispatch — registering multiple VDFs under the same SQL name with different parameter counts. Tracked in [villagesql-server#27](https://github.com/villagesql/villagesql-server/issues/27).

## Reporting Bugs and Requesting Features

Open an issue at [https://github.com/villagesql/villagesql-samples/issues](https://github.com/villagesql/villagesql-samples/issues). Include:

- Title describing the behavior
- Description with expected vs. actual output
- Steps to reproduce (SQL statements, input strings, VillageSQL version)
- Environment details (OS, compiler version, `SELECT @@version`)

## Contact

- [Discord](https://discord.gg/KSr6whd3Fr)
- [GitHub Issues](https://github.com/villagesql/villagesql-samples/issues)
- [GitHub Discussions](https://github.com/villagesql/villagesql-samples/discussions)

## License

GPL-2.0. See license headers in source files for details.

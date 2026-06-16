# AGENTS.md

This file provides guidance to AI coding assistants (Claude Code, Gemini Code Assist, etc.) when working with code in this repository.

**Note**: Also check `AGENTS.local.md` for additional local development instructions when present.

## Project Overview

`vsql-fuzzystrmatch` is a port of PostgreSQL's `fuzzystrmatch` extension for VillageSQL. It provides phonetic and edit-distance string matching: Soundex, Levenshtein edit distance, Metaphone, and Double Metaphone.

Install name (underscores): `vsql_fuzzystrmatch`
GitHub repo name (hyphens): `vsql-fuzzystrmatch`

## Build System

**Configure and Build:**
```bash
mkdir build && cd build
cmake .. -DVillageSQL_BUILD_DIR=/path/to/villagesql/build
make
```

**Install:**
```bash
make install
```

The build:
1. Uses `cmake/FindVillageSQL.cmake` to locate the VillageSQL Extension SDK via `VillageSQL_BUILD_DIR`
2. Compiles `src/vsql_fuzzystrmatch.cc` into a shared library
3. Packages it with `manifest.json` into `vsql_fuzzystrmatch.veb` via `VEF_CREATE_VEB()`
4. `make install` places the VEB into the build tree's `veb_output_directory`

**CMake Variables:**
- `VillageSQL_BUILD_DIR`: Path to VillageSQL build directory (required)

## Architecture

**Core Components:**
- `src/vsql_fuzzystrmatch.cc` — all function implementations and VDF registration in one file
- `cmake/FindVillageSQL.cmake` — SDK discovery module
- `manifest.json` — extension metadata (`vsql_fuzzystrmatch`, version `0.0.1`)
- `mysql-test/t/` — MTR test files
- `mysql-test/r/` — expected MTR results

**Available Functions:**

*Soundex (phonetic, English):*
- `soundex(text)` — returns 4-character Soundex code (e.g. `soundex('Robert')` → `'R163'`)
- `difference(text, text)` — Soundex similarity 0–4; 4 = identical phonetic codes

*Levenshtein edit distance:*
- `levenshtein(source text, target text)` — minimum single-character edits (all operations cost 1)
- `levenshtein_cost(source, target, ins_cost int, del_cost int, sub_cost int)` — configurable per-operation costs
- `levenshtein_less_equal(source, target, max_d int)` — edit distance if ≤ max_d, else max_d+1 (early-exit)
- `levenshtein_less_equal_cost(source, target, ins, del, sub, max_d)` — custom costs + early-exit

*Metaphone (phonetic, English):*
- `metaphone(source text, max_output_length int)` — Metaphone code truncated to max_output_length characters

*Double Metaphone (handles multiple linguistic origins):*
- `dmetaphone(source text)` — primary Double Metaphone code
- `dmetaphone_alt(source text)` — alternate Double Metaphone code (falls back to primary if no alternate)

All functions return NULL if any argument is NULL. Input strings for Levenshtein must be ≤ 255 characters.

**No external dependencies** beyond the VillageSQL SDK.

## VEF API

Uses Protocol V3 (`#include <villagesql/vsql.h>`, `using namespace vsql`). Typed wrappers:
- `StringArg` / `StringResult` for string parameters and return values
- `IntArg` / `IntResult` for integer parameters and return values
- `out.set_null()` for NULL results
- `out.warning("msg")` for user-input validation errors (returns NULL, emits Warning 3200)

## Testing

MTR test suite — 4 test files:
- `soundex` — `soundex()`, `difference()`, edge cases
- `levenshtein` — `levenshtein()`, `levenshtein_cost()`, `levenshtein_less_equal()`, `levenshtein_less_equal_cost()`, NULL handling, input length limits
- `metaphone` — `metaphone()`, truncation, NULL handling
- `dmetaphone` — `dmetaphone()`, `dmetaphone_alt()`, alternates, NULL handling

**Run tests (requires `make install` first):**
```bash
cd /path/to/villagesql/build/mysql-test
perl mysql-test-run.pl --suite=/path/to/vsql-fuzzystrmatch/mysql-test
```

**Re-record results:**
```bash
perl mysql-test-run.pl --suite=/path/to/vsql-fuzzystrmatch/mysql-test --record
```

## Known Limitations

- **No function overloading**: VEF registers VDFs by name only. PostgreSQL's overloaded `levenshtein(s1,s2)` and `levenshtein(s1,s2,ins,del,sub)` cannot coexist under the same name. The cost variants are renamed: `levenshtein_cost` and `levenshtein_less_equal_cost`. Tracked in [villagesql-server#27](https://github.com/villagesql/villagesql-server/issues/27).

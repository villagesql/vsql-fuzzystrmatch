# Testing — vsql_fuzzystrmatch

## Environment

You need two things:

1. **`VillageSQL_BUILD_DIR`** — the path to your VillageSQL build output directory. This is where `cmake --build` put its output (e.g., `~/build/villagesql`). The same directory has `runtime_output_directory/mysqld` in it. The server must be running before you run tests.

2. **Extension installed** — the `.veb` file must be installed in the server's VEB directory before running the test suite. Build and install first (see below).

## Build and Install

```bash
export VillageSQL_BUILD_DIR=/path/to/villagesql/build

cd /path/to/vsql_fuzzystrmatch
./build.sh
cd build && make install
```

`make install` copies the `.veb` to the directory the server reads VEB files from. The path is computed by cmake from `VillageSQL_BUILD_DIR`.

Verify installation:

```bash
/path/to/villagesql/build/runtime_output_directory/mysql -u root \
  -e "INSTALL EXTENSION vsql_fuzzystrmatch; SELECT vsql_fuzzystrmatch.soundex('test'); UNINSTALL EXTENSION vsql_fuzzystrmatch;"
```

## Running Tests

Tests use the MySQL Test Runner (MTR). Run from the server's `mysql-test/` directory:

**Linux:**

```bash
cd $HOME/build/villagesql/mysql-test
perl mysql-test-run.pl --suite=/path/to/vsql_fuzzystrmatch/mysql-test
```

**macOS:**

```bash
cd ~/build/villagesql/mysql-test
perl mysql-test-run.pl --suite=/path/to/vsql_fuzzystrmatch/mysql-test
```

Expected output when all tests pass:

```
[ 20%] vsql_fuzzystrmatch/mysql-test.dmetaphone  [ pass ]
[ 40%] vsql_fuzzystrmatch/mysql-test.levenshtein  [ pass ]
[ 60%] vsql_fuzzystrmatch/mysql-test.metaphone    [ pass ]
[ 80%] vsql_fuzzystrmatch/mysql-test.soundex      [ pass ]
[100%] shutdown_report                            [ pass ]

Completed: All 5 tests were successful.
```

## Regenerating Result Files

If you modify a `.test` file or fix a function's output, regenerate the expected results with the `--record` flag:

```bash
cd $HOME/build/villagesql/mysql-test
perl mysql-test-run.pl --suite=/path/to/vsql_fuzzystrmatch/mysql-test --record
```

This overwrites the `.result` files in `mysql-test/r/` with actual server output. Review the diff before committing.

## Test Coverage

| Test file | Functions covered |
|---|---|
| `t/soundex.test` | `soundex`, `difference`; basic cases, NULL handling |
| `t/levenshtein.test` | `levenshtein`, `levenshtein_cost`, `levenshtein_less_equal`, `levenshtein_less_equal_cost`; basic cases, early-exit semantics, negative cost error, NULL handling |
| `t/metaphone.test` | `metaphone`; basic cases, truncation, NULL handling |
| `t/dmetaphone.test` | `dmetaphone`, `dmetaphone_alt`; primary vs. alternate codes, NULL handling |

Each test file installs the extension at the top and uninstalls at the bottom, so tests are isolated and can run in any order.

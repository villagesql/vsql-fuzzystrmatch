# Architecture — vsql_fuzzystrmatch

Extension: vsql_fuzzystrmatch
Session version: 0.0.4-dev
SDK: villagesql-extension-sdk-0.0.4-dev

---

## API Bootstrap Results (Phase 2)

**SDK version alignment**: 0.0.4-dev matches running server ✓

**Staged SDK headers read**: `extension.h`, `func_builder.h`, `abi/types.h`

**Result type constants** (from `abi/types.h`):
- `VEF_RESULT_VALUE = 0`
- `VEF_RESULT_NULL = 1`
- `VEF_RESULT_ERROR = 2`

**Input struct** (`vef_invalue_t`) fields:
- `type` (vef_type_id) — type of the value
- `is_null` (bool) — check this first on every function
- `str_value` / `str_len` — for STRING inputs
- `int_value` (long long) — for INT inputs
- `real_value` (double) — for REAL inputs
- `bin_value` / `bin_len` — for CUSTOM type inputs

**Result struct** (`vef_vdf_result_t`) fields:
- `type` (vef_return_value_type_t) — set to VALUE/NULL/ERROR
- `actual_len` (size_t) — bytes written (for STRING returns)
- `error_msg` (char*) — snprintf target, max `VEF_MAX_ERROR_LEN` (512)
- `str_buf` / `max_str_len` — for STRING returns
- `int_value` (long long) — for INT returns

**Max VDF parameters**: `kMaxParams = 8` → 6-arg levenshtein_less_equal_cost fits ✓

**Storage model**: N/A — no custom types, all functions return STRING or INT

**Index registration**: N/A — no custom types

**Deterministic builder method**: NOT present in staged SDK (Protocol 1 only) → no CHECK constraint use cases affected for this extension

---

## Design

### No Custom Types

fuzzystrmatch is pure computation — all inputs and outputs are STRING or INT.
No encode/decode/compare/hash functions needed.

### Function Map

| SQL Name | Parameters | Returns | Buffer | Notes |
|---|---|---|---|---|
| `soundex` | STRING | STRING | 4 | Always exactly 4 chars |
| `difference` | STRING, STRING | INT | — | Calls soundex internally |
| `levenshtein` | STRING, STRING | INT | — | 2-arg form |
| `levenshtein_cost` | STRING, STRING, INT, INT, INT | INT | — | ins/del/sub costs |
| `levenshtein_less_equal` | STRING, STRING, INT | INT | — | 3-arg bounded |
| `levenshtein_less_equal_cost` | STRING, STRING, INT, INT, INT, INT | INT | — | 6-arg bounded |
| `metaphone` | STRING, INT | STRING | 64 | max_output_len param; output ≤ param |
| `dmetaphone` | STRING | STRING | 7 | Primary code, max 6 chars |
| `dmetaphone_alt` | STRING | STRING | 7 | Alternate code, max 6 chars |

### Algorithm Notes

**Soundex** (Philips 1918):
- Uppercase first letter as-is
- Map remaining consonants: B,F,P,V=1; C,G,J,K,Q,S,X,Z=2; D,T=3; L=4; M,N=5; R=6
- H and W are transparent (ignored in duplicate detection but not coded)
- Suppress adjacent duplicate codes (including across H/W)
- Drop vowels (A,E,I,O,U) and Y
- Pad with '0' to 4 characters; truncate at 4
- Empty input → empty output

**Difference**:
- Compute soundex(a) and soundex(b)
- Count matching characters at same position (0-4)

**Levenshtein** (Wagner-Fischer DP):
- Space-optimized single-row algorithm O(min(m,n)) space
- For `levenshtein_cost`: same DP with custom ins_cost, del_cost, sub_cost
- For `levenshtein_less_equal`: early-exit when minimum in current row exceeds imax; return imax+1 if exceeded
- Error conditions: negative costs → VEF_RESULT_ERROR; strings > 255 chars (PostgreSQL limit) → VEF_RESULT_ERROR

**Metaphone** (Philips 1990):
- Uppercase all input, strip non-alpha
- Apply initial letter adjustments (AE/GN/KN/PN/WR → drop first letter)
- Encode each character/digraph to a metaphone code
- Truncate result at max_output_len
- Key codes: 0=TH, B,D,F,J,K,L,M,N,P,R,S,T,X all map to themselves or standard substitutions

**Double Metaphone** (Philips 2000):
- Returns primary and alternate codes (for dmetaphone/dmetaphone_alt)
- Handles European name origins: Slavic, German, French, Italian, etc.
- Max output: 6 characters (configurable via constant)

---

## Feasibility Findings

**All functions implementable within VEF Protocol 1.** No blocking constraints.

**Constraints documented in limitations.md:**
- No overload support → 4 functions need distinct names (see limitations.md)

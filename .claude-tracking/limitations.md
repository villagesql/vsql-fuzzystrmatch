# Known Limitations — vsql_fuzzystrmatch

## VEF: No function overloading

**Constraint**: VEF registers VDFs as MySQL UDFs keyed by function name alone.
Two functions with the same name but different parameter counts cannot both be
registered — the second registration fails with "VDF '...' already exists"
(register.cc:71).

**PostgreSQL impact**: PostgreSQL fuzzystrmatch exposes:
- `levenshtein(text, text)` and `levenshtein(text, text, int, int, int)`
- `levenshtein_less_equal(text, text, int)` and `levenshtein_less_equal(text, text, int, int, int, int)`

These four functions collapse to two names in the PostgreSQL API. In VEF they
must be four distinct names.

**Workaround applied**:
- `levenshtein(s1, s2)` → **`LEVENSHTEIN(s1, s2)`**
- `levenshtein(s1, s2, ins, del, sub)` → **`LEVENSHTEIN_COST(s1, s2, ins, del, sub)`**
- `levenshtein_less_equal(s1, s2, max_d)` → **`LEVENSHTEIN_LESS_EQUAL(s1, s2, max_d)`**
- `levenshtein_less_equal(s1, s2, ins, del, sub, max_d)` → **`LEVENSHTEIN_LESS_EQUAL_COST(s1, s2, ins, del, sub, max_d)`**

**VEF hook needed to remove this limitation**: Support for VDF overloading —
registering multiple functions under the same SQL name with different parameter
counts, with the server dispatching by arity at call time.

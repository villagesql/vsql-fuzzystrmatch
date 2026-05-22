# Acceptance Criteria — vsql_fuzzystrmatch

Extension: vsql_fuzzystrmatch
Server version at session start: 0.0.4-dev

Note: PostgreSQL's levenshtein/2 and levenshtein/5 overloads cannot be
registered under the same name in VEF (name-keyed registration). The 5-arg
variant is named levenshtein_cost; the 6-arg levenshtein_less_equal variant
is named levenshtein_less_equal_cost.

---

## Soundex

1. Given 'Robert', `vsql_fuzzystrmatch.soundex('Robert')` must return 'R163'.
2. Given 'Rupert', `vsql_fuzzystrmatch.soundex('Rupert')` must return 'R163' (same code as 'Robert').
3. Given '', `vsql_fuzzystrmatch.soundex('')` must return '' (empty input → empty code).

## difference

4. Given 'Robert' and 'Rupert', `vsql_fuzzystrmatch.difference('Robert', 'Rupert')` must return 4 (identical Soundex codes).
5. Given 'Anne' and 'Ann', `vsql_fuzzystrmatch.difference('Anne', 'Ann')` must return 4 (both map to A500).
6. Given 'Robert' and 'Sylvia', `vsql_fuzzystrmatch.difference('Robert', 'Sylvia')` must return 0 (no Soundex characters in common: R163 vs S410).

## levenshtein (2-arg)

7. Given 'kitten' and 'sitting', `vsql_fuzzystrmatch.levenshtein('kitten', 'sitting')` must return 3.
8. Given '' and 'abc', `vsql_fuzzystrmatch.levenshtein('', 'abc')` must return 3.
9. Given 'abc' and '', `vsql_fuzzystrmatch.levenshtein('abc', '')` must return 3.
10. Given 'abc' and 'abc', `vsql_fuzzystrmatch.levenshtein('abc', 'abc')` must return 0.

## levenshtein_cost (5-arg, custom costs)

11. Given 'kitten', 'sitting', ins_cost=1, del_cost=1, sub_cost=1, `vsql_fuzzystrmatch.levenshtein_cost('kitten', 'sitting', 1, 1, 1)` must return 3.
12. Given 'abc', 'xyz', ins_cost=1, del_cost=1, sub_cost=10, `vsql_fuzzystrmatch.levenshtein_cost('abc', 'xyz', 1, 1, 10)` must return 6 (insert 3 + delete 3 is cheaper than 3 substitutions at cost 10).

## levenshtein_less_equal (3-arg)

13. Given 'kitten', 'sitting', imax=5, `vsql_fuzzystrmatch.levenshtein_less_equal('kitten', 'sitting', 5)` must return 3 (distance ≤ imax).
14. Given 'kitten', 'sitting', imax=2, `vsql_fuzzystrmatch.levenshtein_less_equal('kitten', 'sitting', 2)` must return 3 (distance > imax; returns imax+1 = 3).

## levenshtein_less_equal_cost (6-arg, custom costs)

15. Given 'abc', 'xyz', ins=1, del=1, sub=1, imax=5, `vsql_fuzzystrmatch.levenshtein_less_equal_cost('abc', 'xyz', 1, 1, 1, 5)` must return 3 (distance ≤ imax).
16. Given 'abc', 'xyz', ins=1, del=1, sub=1, imax=2, `vsql_fuzzystrmatch.levenshtein_less_equal_cost('abc', 'xyz', 1, 1, 1, 2)` must return 3 (distance > imax; returns imax+1 = 3).

## metaphone

17. Given 'Robert' with max_output_len=10, `vsql_fuzzystrmatch.metaphone('Robert', 10)` must return 'RBRT'.
18. Given 'Thompson' with max_output_len=10, `vsql_fuzzystrmatch.metaphone('Thompson', 10)` must return '0MPSN' (TH → 0 in original Philips algorithm).
19. Given 'Robert' with max_output_len=2, `vsql_fuzzystrmatch.metaphone('Robert', 2)` must return 'RB' (truncated to max_output_len).

## dmetaphone / dmetaphone_alt

20. Given 'Robert', `vsql_fuzzystrmatch.dmetaphone('Robert')` must return 'RPRT'.
21. Given 'Robert', `vsql_fuzzystrmatch.dmetaphone_alt('Robert')` must return 'RPRT'.
22. Given 'Schmidt', `vsql_fuzzystrmatch.dmetaphone('Schmidt')` must return 'XMT'.
23. Given 'Schmidt', `vsql_fuzzystrmatch.dmetaphone_alt('Schmidt')` must return 'SMT'.

## NULL handling

24. Given NULL input, `vsql_fuzzystrmatch.soundex(NULL)` must return NULL.
25. Given NULL input, `vsql_fuzzystrmatch.levenshtein(NULL, 'abc')` must return NULL.
26. Given NULL input, `vsql_fuzzystrmatch.metaphone(NULL, 10)` must return NULL.

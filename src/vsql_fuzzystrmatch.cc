/* Copyright (c) 2026 VillageSQL Contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <villagesql/extension.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <string>
#include <string_view>

using namespace villagesql::extension_builder;
using namespace villagesql::func_builder;

// =============================================================================
// Soundex helpers
// =============================================================================

static char soundex_code(char c) {
  switch (std::toupper(static_cast<unsigned char>(c))) {
    case 'B': case 'F': case 'P': case 'V': return '1';
    case 'C': case 'G': case 'J': case 'K':
    case 'Q': case 'S': case 'X': case 'Z': return '2';
    case 'D': case 'T': return '3';
    case 'L': return '4';
    case 'M': case 'N': return '5';
    case 'R': return '6';
    default: return '0';
  }
}

static std::string compute_soundex(std::string_view input) {
  if (input.empty()) return "";

  // Find the first alpha character
  size_t start = 0;
  while (start < input.size() &&
         !std::isalpha(static_cast<unsigned char>(input[start]))) {
    ++start;
  }
  if (start == input.size()) return "";

  char first = static_cast<char>(
      std::toupper(static_cast<unsigned char>(input[start])));
  std::string out;
  out.reserve(4);
  out += first;

  char prev_code = soundex_code(first);

  for (size_t i = start + 1; i < input.size() && out.size() < 4; ++i) {
    unsigned char c = static_cast<unsigned char>(input[i]);
    if (!std::isalpha(c)) continue;

    // H and W are transparent — they don't break duplicate suppression
    if (c == 'H' || c == 'h' || c == 'W' || c == 'w') continue;

    char code = soundex_code(static_cast<char>(c));
    if (code == '0') {
      // Vowels reset the previous code so the next consonant is not suppressed
      prev_code = '0';
      continue;
    }
    if (code != prev_code) {
      out += code;
      prev_code = code;
    }
  }

  while (out.size() < 4) out += '0';
  return out;
}

// =============================================================================
// Soundex
// =============================================================================

void soundex_impl(vef_context_t *ctx, vef_invalue_t *arg,
                  vef_vdf_result_t *result) {
  try {
    if (arg->is_null) {
      result->type = VEF_RESULT_NULL;
      return;
    }
    std::string s = compute_soundex({arg->str_value, arg->str_len});
    if (s.size() > result->max_str_len) {
      result->type = VEF_RESULT_ERROR;
      snprintf(result->error_msg, VEF_MAX_ERROR_LEN, "soundex: buffer too small");
      return;
    }
    std::memcpy(result->str_buf, s.data(), s.size());
    result->actual_len = s.size();
    result->type = VEF_RESULT_VALUE;
  } catch (...) {
    result->type = VEF_RESULT_ERROR;
    snprintf(result->error_msg, VEF_MAX_ERROR_LEN, "soundex: internal error");
  }
}

// =============================================================================
// difference
// =============================================================================

void difference_impl(vef_context_t *ctx, vef_invalue_t *a, vef_invalue_t *b,
                     vef_vdf_result_t *result) {
  try {
    if (a->is_null || b->is_null) {
      result->type = VEF_RESULT_NULL;
      return;
    }
    std::string sa = compute_soundex({a->str_value, a->str_len});
    std::string sb = compute_soundex({b->str_value, b->str_len});
    // Pad to length 4 for comparison (empty inputs yield "")
    while (sa.size() < 4) sa += '0';
    while (sb.size() < 4) sb += '0';
    long long score = 0;
    for (size_t i = 0; i < 4; ++i) {
      if (sa[i] == sb[i]) ++score;
    }
    result->int_value = score;
    result->type = VEF_RESULT_VALUE;
  } catch (...) {
    result->type = VEF_RESULT_ERROR;
    snprintf(result->error_msg, VEF_MAX_ERROR_LEN, "difference: internal error");
  }
}

// =============================================================================
// Levenshtein helpers
// =============================================================================

// Maximum string length accepted (PostgreSQL compatibility limit)
static constexpr size_t kLevenshteinMaxLen = 255;

// Core DP with custom costs and optional early-exit at imax.
// Returns -1 if distance exceeds imax (when imax >= 0).
static long long levenshtein_dp(std::string_view src, std::string_view tgt,
                                long long ins_cost, long long del_cost,
                                long long sub_cost, long long imax) {
  // Ensure src is the shorter string to minimise memory
  if (src.size() > tgt.size()) {
    std::swap(src, tgt);
    std::swap(ins_cost, del_cost);
  }

  size_t m = src.size();
  size_t n = tgt.size();

  if (src == tgt) return 0;
  if (m == 0) return static_cast<long long>(n) * ins_cost;

  long long prev_arr[kLevenshteinMaxLen + 1];
  long long curr_arr[kLevenshteinMaxLen + 1];
  long long *prev = prev_arr;
  long long *cur = curr_arr;
  for (size_t i = 0; i <= m; ++i) prev[i] = static_cast<long long>(i) * del_cost;

  for (size_t j = 1; j <= n; ++j) {
    cur[0] = static_cast<long long>(j) * ins_cost;
    long long row_min = cur[0];
    for (size_t i = 1; i <= m; ++i) {
      long long cost = (src[i - 1] == tgt[j - 1]) ? 0 : sub_cost;
      cur[i] = std::min({prev[i - 1] + cost,
                         prev[i] + ins_cost,
                         cur[i - 1] + del_cost});
      row_min = std::min(row_min, cur[i]);
    }
    if (imax >= 0 && row_min > imax) return -1;
    std::swap(prev, cur);
  }
  return prev[m];
}

static bool check_lev_len(std::string_view sa, std::string_view sb,
                           const char *fn, vef_vdf_result_t *result) {
  if (sa.size() > kLevenshteinMaxLen || sb.size() > kLevenshteinMaxLen) {
    result->type = VEF_RESULT_ERROR;
    snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
             "%s: argument exceeds maximum length of %zu", fn,
             kLevenshteinMaxLen);
    return false;
  }
  return true;
}

// =============================================================================
// levenshtein (2-arg)
// =============================================================================

void levenshtein_impl(vef_context_t *ctx, vef_invalue_t *a, vef_invalue_t *b,
                      vef_vdf_result_t *result) {
  try {
    if (a->is_null || b->is_null) {
      result->type = VEF_RESULT_NULL;
      return;
    }
    std::string_view sa{a->str_value, a->str_len};
    std::string_view sb{b->str_value, b->str_len};
    if (!check_lev_len(sa, sb, "levenshtein", result)) return;
    result->int_value = levenshtein_dp(sa, sb, 1, 1, 1, -1);
    result->type = VEF_RESULT_VALUE;
  } catch (...) {
    result->type = VEF_RESULT_ERROR;
    snprintf(result->error_msg, VEF_MAX_ERROR_LEN, "levenshtein: internal error");
  }
}

// =============================================================================
// levenshtein_cost (5-arg)
// =============================================================================

void levenshtein_cost_impl(vef_context_t *ctx, vef_invalue_t *a,
                           vef_invalue_t *b, vef_invalue_t *ins,
                           vef_invalue_t *del, vef_invalue_t *sub,
                           vef_vdf_result_t *result) {
  try {
    if (a->is_null || b->is_null || ins->is_null || del->is_null ||
        sub->is_null) {
      result->type = VEF_RESULT_NULL;
      return;
    }
    long long ic = ins->int_value, dc = del->int_value, sc = sub->int_value;
    if (ic < 0 || dc < 0 || sc < 0) {
      result->type = VEF_RESULT_ERROR;
      snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
               "levenshtein_cost: costs must be non-negative");
      return;
    }
    if (ic > INT_MAX || dc > INT_MAX || sc > INT_MAX) {
      result->type = VEF_RESULT_ERROR;
      snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
               "levenshtein_cost: costs must not exceed %d", INT_MAX);
      return;
    }
    std::string_view sa{a->str_value, a->str_len};
    std::string_view sb{b->str_value, b->str_len};
    if (!check_lev_len(sa, sb, "levenshtein_cost", result)) return;
    result->int_value = levenshtein_dp(sa, sb, ic, dc, sc, -1);
    result->type = VEF_RESULT_VALUE;
  } catch (...) {
    result->type = VEF_RESULT_ERROR;
    snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
             "levenshtein_cost: internal error");
  }
}

// =============================================================================
// levenshtein_less_equal (3-arg)
// =============================================================================

void levenshtein_less_equal_impl(vef_context_t *ctx, vef_invalue_t *a,
                                 vef_invalue_t *b, vef_invalue_t *imax,
                                 vef_vdf_result_t *result) {
  try {
    if (a->is_null || b->is_null || imax->is_null) {
      result->type = VEF_RESULT_NULL;
      return;
    }
    long long max_d = imax->int_value;
    if (max_d < 0) {
      result->type = VEF_RESULT_ERROR;
      snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
               "levenshtein_less_equal: max_d must be non-negative");
      return;
    }
    std::string_view sa{a->str_value, a->str_len};
    std::string_view sb{b->str_value, b->str_len};
    if (!check_lev_len(sa, sb, "levenshtein_less_equal", result)) return;
    long long d = levenshtein_dp(sa, sb, 1, 1, 1, max_d);
    result->int_value = (d < 0) ? (max_d < LLONG_MAX ? max_d + 1 : max_d) : d;
    result->type = VEF_RESULT_VALUE;
  } catch (...) {
    result->type = VEF_RESULT_ERROR;
    snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
             "levenshtein_less_equal: internal error");
  }
}

// =============================================================================
// levenshtein_less_equal_cost (6-arg)
// =============================================================================

void levenshtein_less_equal_cost_impl(vef_context_t *ctx, vef_invalue_t *a,
                                      vef_invalue_t *b, vef_invalue_t *ins,
                                      vef_invalue_t *del, vef_invalue_t *sub,
                                      vef_invalue_t *imax,
                                      vef_vdf_result_t *result) {
  try {
    if (a->is_null || b->is_null || ins->is_null || del->is_null ||
        sub->is_null || imax->is_null) {
      result->type = VEF_RESULT_NULL;
      return;
    }
    long long ic = ins->int_value, dc = del->int_value, sc = sub->int_value;
    long long max_d = imax->int_value;
    if (ic < 0 || dc < 0 || sc < 0) {
      result->type = VEF_RESULT_ERROR;
      snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
               "levenshtein_less_equal_cost: costs must be non-negative");
      return;
    }
    if (ic > INT_MAX || dc > INT_MAX || sc > INT_MAX) {
      result->type = VEF_RESULT_ERROR;
      snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
               "levenshtein_less_equal_cost: costs must not exceed %d", INT_MAX);
      return;
    }
    if (max_d < 0) {
      result->type = VEF_RESULT_ERROR;
      snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
               "levenshtein_less_equal_cost: max_d must be non-negative");
      return;
    }
    std::string_view sa{a->str_value, a->str_len};
    std::string_view sb{b->str_value, b->str_len};
    if (!check_lev_len(sa, sb, "levenshtein_less_equal_cost", result)) return;
    long long d = levenshtein_dp(sa, sb, ic, dc, sc, max_d);
    result->int_value = (d < 0) ? (max_d < LLONG_MAX ? max_d + 1 : max_d) : d;
    result->type = VEF_RESULT_VALUE;
  } catch (...) {
    result->type = VEF_RESULT_ERROR;
    snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
             "levenshtein_less_equal_cost: internal error");
  }
}

// =============================================================================
// Metaphone (Philips 1990)
// =============================================================================

static std::string compute_metaphone(std::string_view input, int max_out) {
  // Uppercase and strip non-alpha
  std::string w;
  w.reserve(input.size());
  for (unsigned char c : input) {
    if (std::isalpha(c)) w += static_cast<char>(std::toupper(c));
  }
  if (w.empty() || max_out <= 0) return "";

  // Initial letter transformations
  if (w.size() >= 2) {
    std::string_view pfx(w.data(), 2);
    if (pfx == "AE" || pfx == "GN" || pfx == "KN" || pfx == "PN" ||
        pfx == "WR") {
      w.erase(0, 1);
    }
  }
  if (!w.empty() && w[0] == 'I') w[0] = 'E';  // initial vowels all → same

  auto at = [&](size_t i) -> char {
    return (i < w.size()) ? w[i] : '\0';
  };

  std::string out;
  out.reserve(static_cast<size_t>(max_out) + 1);
  size_t i = 0;
  bool prev_was_vowel = false;

  while (i < w.size() && static_cast<int>(out.size()) < max_out) {
    char c = at(i);
    char n = at(i + 1);
    char n2 = at(i + 2);
    char p = (i > 0) ? w[i - 1] : '\0';

    // Drop duplicate adjacent letters, except C
    if (c == p && c != 'C') { ++i; continue; }

    switch (c) {
      case 'A': case 'E': case 'I': case 'O': case 'U':
        // Initial vowel coded; subsequent vowels ignored
        if (i == 0) out += c;
        prev_was_vowel = true;
        ++i; continue;

      case 'B':
        // Silent B after M at end of word
        if (!(p == 'M' && i + 1 == w.size())) out += 'B';
        break;

      case 'C':
        if (n == 'I' || n == 'E' || n == 'Y') {
          out += 'S';
        } else if (n == 'H') {
          out += 'X'; ++i;
        } else {
          out += 'K';
        }
        break;

      case 'D':
        if (n == 'G' && (n2 == 'E' || n2 == 'I' || n2 == 'Y')) {
          out += 'J'; ++i;
        } else {
          out += 'T';
        }
        break;

      case 'F': out += 'F'; break;

      case 'G':
        if (n == 'H') {
          // GH silent unless at beginning or before vowel
          if (i + 1 == w.size()) { ++i; break; }
          // GH at start or GH not after vowel
          if (i == 0 || (!prev_was_vowel)) { out += 'K'; ++i; break; }
          ++i; break;  // silent
        }
        if (n == 'N') {
          // GN at end or GNED at end → silent G
          if (i + 1 == w.size() ||
              (i + 3 == w.size() && n == 'N' && n2 == 'E' && at(i + 3) == 'D')) {
            break;
          }
        }
        if ((n == 'E' || n == 'I' || n == 'Y') && p != 'G') {
          out += 'J';
        } else {
          out += 'K';
        }
        break;

      case 'H':
        // H before vowel (and not after vowel)
        if (!prev_was_vowel && (n == 'A' || n == 'E' || n == 'I' || n == 'O' ||
                                 n == 'U')) {
          out += 'H';
        }
        break;

      case 'J': out += 'J'; break;
      case 'K':
        if (p != 'C') out += 'K';
        break;

      case 'L': out += 'L'; break;
      case 'M': out += 'M'; break;
      case 'N': out += 'N'; break;

      case 'P':
        if (n == 'H') { out += 'F'; ++i; }
        else out += 'P';
        break;

      case 'Q': out += 'K'; break;
      case 'R': out += 'R'; break;

      case 'S':
        if (n == 'H' || (n == 'I' && (n2 == 'O' || n2 == 'A'))) {
          out += 'X'; if (n == 'H') ++i;
        } else {
          out += 'S';
        }
        break;

      case 'T':
        if (n == 'H') { out += '0'; ++i; }
        else if (n == 'I' && (n2 == 'A' || n2 == 'O')) { out += 'X'; }
        else out += 'T';
        break;

      case 'V': out += 'F'; break;
      case 'W':
        if (n == 'H') ++i;  // WH → drop H
        if (n == 'A' || n == 'E' || n == 'I' || n == 'O' || n == 'U' ||
            n == 'H') {
          out += 'W';
        }
        break;

      case 'X': out += "KS"; break;

      case 'Y':
        if (n == 'A' || n == 'E' || n == 'I' || n == 'O' || n == 'U') {
          out += 'Y';
        }
        break;

      case 'Z': out += 'S'; break;

      default: break;
    }

    prev_was_vowel = (c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U');
    ++i;
  }

  if (static_cast<int>(out.size()) > max_out) out.resize(static_cast<size_t>(max_out));
  return out;
}

void metaphone_impl(vef_context_t *ctx, vef_invalue_t *str,
                    vef_invalue_t *max_len, vef_vdf_result_t *result) {
  try {
    if (str->is_null || max_len->is_null) {
      result->type = VEF_RESULT_NULL;
      return;
    }
    long long ml = max_len->int_value;
    if (ml <= 0) {
      result->type = VEF_RESULT_ERROR;
      snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
               "metaphone: max_output_length must be > 0");
      return;
    }
    if (ml > static_cast<long long>(INT_MAX)) {
      result->type = VEF_RESULT_ERROR;
      snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
               "metaphone: max_output_length must not exceed %d", INT_MAX);
      return;
    }
    std::string code =
        compute_metaphone({str->str_value, str->str_len}, static_cast<int>(ml));
    if (code.size() > result->max_str_len) {
      result->type = VEF_RESULT_ERROR;
      snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
               "metaphone: output buffer too small");
      return;
    }
    std::memcpy(result->str_buf, code.data(), code.size());
    result->actual_len = code.size();
    result->type = VEF_RESULT_VALUE;
  } catch (...) {
    result->type = VEF_RESULT_ERROR;
    snprintf(result->error_msg, VEF_MAX_ERROR_LEN, "metaphone: internal error");
  }
}

// =============================================================================
// Double Metaphone (Philips 2000)
// =============================================================================

static constexpr int kDMetaphoneMaxLen = 6;

struct DMetaphoneResult {
  std::string primary;
  std::string alternate;
};

// Helper: is character a vowel?
static bool dm_is_vowel(char c) {
  return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' ||
         c == 'Y';
}

// Helper: string contains substring at position
static bool dm_string_at(const std::string &w, int pos,
                         std::initializer_list<const char *> substrs) {
  if (pos < 0 || static_cast<size_t>(pos) >= w.size()) return false;
  for (const char *s : substrs) {
    size_t slen = std::strlen(s);
    if (static_cast<size_t>(pos) + slen <= w.size() &&
        w.compare(static_cast<size_t>(pos), slen, s) == 0) {
      return true;
    }
  }
  return false;
}

static DMetaphoneResult compute_dmetaphone(std::string_view input) {
  // Build uppercase working string with sentinel spaces
  std::string w = "  ";
  for (unsigned char c : input) {
    if (std::isalpha(c)) w += static_cast<char>(std::toupper(c));
  }
  w += "     ";

  int last = static_cast<int>(w.size()) - 6;  // last real char index

  DMetaphoneResult res;
  res.primary.reserve(kDMetaphoneMaxLen);
  res.alternate.reserve(kDMetaphoneMaxLen);

  auto add = [&](const char *p, const char *a = nullptr) {
    if (p && *p && static_cast<int>(res.primary.size()) < kDMetaphoneMaxLen)
      res.primary += p;
    const char *alt = a ? a : p;
    if (alt && *alt && static_cast<int>(res.alternate.size()) < kDMetaphoneMaxLen)
      res.alternate += alt;
  };

  // Start at offset 2 (sentinel)
  int i = 2;

  // Initial character special cases
  if (dm_string_at(w, i, {"GN", "KN", "PN", "AE", "WR"})) ++i;

  // Initial vowel → all map to 'A'
  if (dm_is_vowel(w[i])) { add("A"); ++i; }

  while (static_cast<int>(res.primary.size()) < kDMetaphoneMaxLen ||
         static_cast<int>(res.alternate.size()) < kDMetaphoneMaxLen) {
    if (i > last) break;
    char c = w[i];

    switch (c) {
      case 'A': case 'E': case 'I': case 'O': case 'U': case 'Y':
        if (i == 2) add("A");
        ++i; continue;

      case 'B':
        add("P");
        i += (w[i + 1] == 'B') ? 2 : 1;
        continue;

      case 'C':
        // Various C rules
        if (i > 2 && !dm_is_vowel(w[i - 2]) && dm_string_at(w, i - 1, {"ACH"}) &&
            w[i + 2] != 'I' &&
            (w[i + 2] != 'E' ||
             dm_string_at(w, i - 2, {"BACHER", "MACHER"}))) {
          add("K"); i += 2; continue;
        }
        if (i == 2 && dm_string_at(w, i, {"CAESAR"})) {
          add("S"); i += 2; continue;
        }
        if (dm_string_at(w, i, {"CHIA"})) { add("K"); i += 2; continue; }
        if (dm_string_at(w, i, {"CH"})) {
          if (i > 2 && dm_string_at(w, i, {"CHAE"})) {
            add("K", "X"); i += 2; continue;
          }
          if (i == 2 && (dm_string_at(w, i + 1, {"HARAC", "HARIS"}) ||
                         dm_string_at(w, i + 1, {"HOR", "HYM", "HIA", "HEM"})) &&
              !dm_string_at(w, 2, {"CHORE"})) {
            add("K"); i += 2; continue;
          }
          if (dm_string_at(w, 2, {"VAN ", "VON "}) ||
              dm_string_at(w, 2, {"SCH"}) ||
              dm_string_at(w, i - 2, {"ORCHES", "ARCHIT", "ORCHID"}) ||
              dm_string_at(w, i + 2, {"T", "S"}) ||
              ((dm_string_at(w, i - 1, {"A", "O", "U", "E"}) || i == 2) &&
               dm_string_at(w, i + 2,
                            {"L", "R", "N", "M", "B", "H", "F", "V", "W"}))) {
            add("K");
          } else {
            add("X");
          }
          i += 2; continue;
        }
        if (dm_string_at(w, i, {"CZ"}) && !dm_string_at(w, i - 2, {"WICZ"})) {
          add("S", "X"); i += 2; continue;
        }
        if (dm_string_at(w, i + 1, {"IA"})) { add("X"); i += 2; continue; }
        if (dm_string_at(w, i, {"CI", "CE", "CY"})) {
          add(dm_string_at(w, i, {"CIO", "CIE", "CIA"}) ? "X" : "S");
          i += 2; continue;
        }
        add("K");
        if (dm_string_at(w, i + 1, {" C", " Q", " G"})) i += 3;
        else if (dm_string_at(w, i + 1, {"C", "K", "Q"}) &&
                 !dm_string_at(w, i + 1, {"CE", "CI"})) i += 2;
        else ++i;
        continue;

      case 'D':
        if (dm_string_at(w, i, {"DG"})) {
          if (dm_string_at(w, i + 2, {"I", "E", "Y"})) {
            add("J"); i += 3;
          } else {
            add("TK"); i += 2;
          }
          continue;
        }
        if (dm_string_at(w, i, {"DT", "DD"})) { add("T"); i += 2; continue; }
        add("T"); ++i; continue;

      case 'F':
        i += (w[i + 1] == 'F') ? 2 : 1;
        add("F"); continue;

      case 'G':
        if (w[i + 1] == 'H') {
          if (i > 2 && !dm_is_vowel(w[i - 1])) { add("K"); i += 2; continue; }
          if (i == 2) {
            if (w[i + 2] == 'I') { add("J"); } else { add("K"); }
            i += 2; continue;
          }
          if ((i > 3 && (w[i - 2] == 'B' || w[i - 2] == 'H' || w[i - 2] == 'D')) ||
              (i > 4 && (w[i - 3] == 'B' || w[i - 3] == 'H' || w[i - 3] == 'D')) ||
              (i > 5 && (w[i - 4] == 'B' || w[i - 4] == 'H'))) {
            i += 2; continue;
          }
          if (i > 2 && w[i - 1] == 'U' &&
              dm_string_at(w, i - 3, {"C", "G", "L", "R", "T"})) {
            add("F"); i += 2; continue;
          }
          if (i > 1 && w[i - 1] != 'I') { add("K"); }
          i += 2; continue;
        }
        if (w[i + 1] == 'N') {
          if (i == 2 && dm_is_vowel(w[2]) && !dm_string_at(w, 2, {"GNOME"})) {
            add("KN", "N");
          } else {
            if (!dm_string_at(w, i + 2, {"EY"}) && w[i + 1] != 'Y' &&
                !dm_string_at(w, 2, {"VAN ", "VON "}) &&
                !dm_string_at(w, 2, {"SCH"})) {
              add("N", "KN");
            } else {
              add("KN");
            }
          }
          i += 2; continue;
        }
        if (dm_string_at(w, i + 1, {"LI"}) &&
            !dm_string_at(w, 2, {"VAN ", "VON "}) &&
            !dm_string_at(w, 2, {"SCH"})) {
          add("KL", "L"); i += 2; continue;
        }
        if (i == 2 &&
            (w[i + 1] == 'Y' ||
             dm_string_at(w, i + 1,
                          {"ES", "EP", "EB", "EL", "EY", "IB", "IL", "IN",
                           "IE", "EI", "ER"}))) {
          add("K", "J"); i += 2; continue;
        }
        if ((dm_string_at(w, i + 1, {"ER"}) || w[i + 1] == 'Y') &&
            !dm_string_at(w, 2, {"DANGER", "RANGER", "MANGER"}) &&
            !dm_string_at(w, i - 1, {"E", "I"}) &&
            !dm_string_at(w, i - 1, {"RGY", "OGY"})) {
          add("K", "J"); i += 2; continue;
        }
        if (dm_string_at(w, i + 1, {"E", "I", "Y"}) ||
            dm_string_at(w, i - 1, {"AGGI", "OGGI"})) {
          if (dm_string_at(w, 2, {"VAN ", "VON "}) ||
              dm_string_at(w, 2, {"SCH"}) || dm_string_at(w, i + 1, {"ET"})) {
            add("K");
          } else {
            add("J", "K");
          }
          i += 2; continue;
        }
        if (w[i + 1] == 'G') i += 2; else ++i;
        add("K"); continue;

      case 'H':
        if ((i == 2 || !dm_is_vowel(w[i - 1])) &&
            dm_is_vowel(w[i + 1])) {
          add("H"); i += 2;
        } else {
          ++i;
        }
        continue;

      case 'J':
        if (dm_string_at(w, i, {"JOSE"}) || dm_string_at(w, 2, {"SAN "})) {
          if ((i == 2 && w[i + 3] == ' ') || dm_string_at(w, 2, {"SAN "})) {
            add("H");
          } else {
            add("J", "H");
          }
          ++i; continue;
        }
        if (i == 2 && !dm_string_at(w, 2, {"JOSE"})) {
          add("J", "A");
        } else if (!dm_is_vowel(w[i - 1]) &&
                   !dm_string_at(w, 2, {"VAN ", "VON "}) &&
                   !dm_string_at(w, 2, {"SCH"}) &&
                   w[i + 1] != 'J') {
          add("J", "H");
        } else if (w[i + 1] == 'J') {
          i += 2; continue;
        } else {
          add("J");
        }
        if (w[i + 1] == 'J') i += 2; else ++i;
        continue;

      case 'K':
        if (w[i + 1] == 'K') i += 2; else ++i;
        add("K"); continue;

      case 'L':
        if (w[i + 1] == 'L') {
          if ((i == last - 2 &&
               dm_string_at(w, i - 1, {"ILLO", "ILLA", "ALLE"})) ||
              ((dm_string_at(w, last - 1, {"AS", "OS"}) ||
                dm_string_at(w, last, {"A", "O"})) &&
               dm_string_at(w, i - 1, {"ALLE"}))) {
            add("L", "");
            i += 2; continue;
          }
          i += 2;
        } else {
          ++i;
        }
        add("L"); continue;

      case 'M':
        if ((dm_string_at(w, i - 1, {"UMB"}) &&
             (i + 1 == last || dm_string_at(w, i + 2, {"ER"}))) ||
            w[i + 1] == 'M') {
          i += 2;
        } else {
          ++i;
        }
        add("M"); continue;

      case 'N':
        i += (w[i + 1] == 'N') ? 2 : 1;
        add("N"); continue;

      case 'P':
        if (w[i + 1] == 'H') {
          add("F"); i += 2; continue;
        }
        i += (w[i + 1] == 'P') ? 2 : 1;
        add("P"); continue;

      case 'Q':
        i += (w[i + 1] == 'Q') ? 2 : 1;
        add("K"); continue;

      case 'R':
        if (i == last && !dm_string_at(w, 2, {"VAN ", "VON "}) &&
            !dm_string_at(w, 2, {"SCH"}) &&
            !dm_string_at(w, i - 2, {"IE"}) &&
            dm_string_at(w, i - 2, {"ME", "MA"})) {
          add("", "R");
        } else {
          add("R");
        }
        i += (w[i + 1] == 'R') ? 2 : 1;
        continue;

      case 'S':
        if (dm_string_at(w, i - 1, {"ISL", "YSL"})) { ++i; continue; }
        if (i == 2 && dm_string_at(w, i, {"SUGAR"})) {
          add("X", "S"); ++i; continue;
        }
        if (dm_string_at(w, i, {"SH"})) {
          if (dm_string_at(w, i + 1,
                           {"HEIM", "HOEK", "HOLM", "HOLZ"})) {
            add("S");
          } else {
            add("X");
          }
          i += 2; continue;
        }
        if (dm_string_at(w, i, {"SIO", "SIA"})) {
          add(dm_string_at(w, 2, {"VAN ", "VON "}) || dm_string_at(w, 2, {"SCH"}) ? "S" : "S", "X");
          ++i; continue;
        }
        if ((i == 2 && dm_string_at(w, i + 1, {"M", "N", "L", "W"})) ||
            dm_string_at(w, i + 1, {"Z"})) {
          add("S", "X");
          if (w[i + 1] == 'Z') i += 2; else ++i;
          continue;
        }
        if (dm_string_at(w, i, {"SC"})) {
          if (w[i + 2] == 'H') {
            if (dm_string_at(w, i + 3,
                             {"OO", "ER", "EN", "UY", "ED", "EM"})) {
              add(dm_string_at(w, i + 3, {"ER", "EN"}) ? "X" : "SK");
            } else if (i == 2 && !dm_is_vowel(w[i + 3]) &&
                       w[i + 3] != 'W') {
              add("X", "S");
            } else {
              add("X");
            }
            i += 3; continue;
          }
          if (dm_string_at(w, i + 2, {"I", "E", "Y"})) {
            add("S"); i += 3; continue;
          }
          add("SK"); i += 3; continue;
        }
        if (i == last && dm_string_at(w, i - 2, {"AI", "OI"})) {
          add("", "S");
        } else {
          add("S");
        }
        i += (w[i + 1] == 'S') ? 2 : 1;
        continue;

      case 'T':
        if (dm_string_at(w, i, {"TION", "TIA", "TCH"})) {
          add("X"); ++i; continue;
        }
        if (dm_string_at(w, i, {"TH"}) || dm_string_at(w, i, {"TTH"})) {
          if (dm_string_at(w, i + 2, {"OM", "AM"}) ||
              dm_string_at(w, 2, {"VAN ", "VON "}) ||
              dm_string_at(w, 2, {"SCH"})) {
            add("T");
          } else {
            add("0", "T");
          }
          i += 2; continue;
        }
        i += (w[i + 1] == 'T' || w[i + 1] == 'D') ? 2 : 1;
        add("T"); continue;

      case 'V':
        i += (w[i + 1] == 'V') ? 2 : 1;
        add("F"); continue;

      case 'W':
        if (dm_string_at(w, i, {"WR"})) { add("R"); i += 2; continue; }
        if (i == 2 && (dm_is_vowel(w[i + 1]) || dm_string_at(w, i, {"WH"}))) {
          if (dm_is_vowel(w[i + 1])) { add("A", "F"); }
          else { add("A"); }
          ++i; continue;
        }
        if ((i == last && dm_is_vowel(w[i - 1])) ||
            dm_string_at(w, i - 1,
                         {"EWSKI", "EWSKY", "OWSKI", "OWSKY"}) ||
            dm_string_at(w, 2, {"SCH"})) {
          add("", "F"); ++i; continue;
        }
        if (dm_string_at(w, i, {"WICZ", "WITZ"})) {
          add("TS", "FX"); i += 4; continue;
        }
        ++i; continue;

      case 'X':
        if (!(i == last &&
              (dm_string_at(w, i - 3, {"IAU", "EAU"}) ||
               dm_string_at(w, i - 2, {"AU", "OU"})))) {
          add("KS");
        }
        i += (w[i + 1] == 'C' || w[i + 1] == 'X') ? 2 : 1;
        continue;

      case 'Z':
        if (w[i + 1] == 'H') {
          add("J"); i += 2; continue;
        }
        if (dm_string_at(w, i + 1, {"ZO", "ZI", "ZA"}) ||
            (i > 2 && w[i - 1] != 'T' &&
             !dm_string_at(w, 2, {"VAN ", "VON "}) &&
             !dm_string_at(w, 2, {"SCH"}))) {
          add("S", "TS");
        } else {
          add("S");
        }
        i += (w[i + 1] == 'Z') ? 2 : 1;
        continue;

      default:
        ++i; continue;
    }
    ++i;
  }

  if (static_cast<int>(res.primary.size()) > kDMetaphoneMaxLen)
    res.primary.resize(static_cast<size_t>(kDMetaphoneMaxLen));
  if (static_cast<int>(res.alternate.size()) > kDMetaphoneMaxLen)
    res.alternate.resize(static_cast<size_t>(kDMetaphoneMaxLen));
  if (res.alternate == res.primary) res.alternate.clear();
  return res;
}

void dmetaphone_impl(vef_context_t *ctx, vef_invalue_t *str,
                     vef_vdf_result_t *result) {
  try {
    if (str->is_null) {
      result->type = VEF_RESULT_NULL;
      return;
    }
    DMetaphoneResult r = compute_dmetaphone({str->str_value, str->str_len});
    const std::string &code = r.primary;
    if (code.size() > result->max_str_len) {
      result->type = VEF_RESULT_ERROR;
      snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
               "dmetaphone: output buffer too small");
      return;
    }
    std::memcpy(result->str_buf, code.data(), code.size());
    result->actual_len = code.size();
    result->type = VEF_RESULT_VALUE;
  } catch (...) {
    result->type = VEF_RESULT_ERROR;
    snprintf(result->error_msg, VEF_MAX_ERROR_LEN, "dmetaphone: internal error");
  }
}

void dmetaphone_alt_impl(vef_context_t *ctx, vef_invalue_t *str,
                         vef_vdf_result_t *result) {
  try {
    if (str->is_null) {
      result->type = VEF_RESULT_NULL;
      return;
    }
    DMetaphoneResult r = compute_dmetaphone({str->str_value, str->str_len});
    // If no alternate exists, return primary (PostgreSQL behaviour)
    const std::string &code = r.alternate.empty() ? r.primary : r.alternate;
    if (code.size() > result->max_str_len) {
      result->type = VEF_RESULT_ERROR;
      snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
               "dmetaphone_alt: output buffer too small");
      return;
    }
    std::memcpy(result->str_buf, code.data(), code.size());
    result->actual_len = code.size();
    result->type = VEF_RESULT_VALUE;
  } catch (...) {
    result->type = VEF_RESULT_ERROR;
    snprintf(result->error_msg, VEF_MAX_ERROR_LEN,
             "dmetaphone_alt: internal error");
  }
}

// =============================================================================
// Registration
// =============================================================================

VEF_GENERATE_ENTRY_POINTS(
    make_extension("vsql_fuzzystrmatch", "1.0.0")
        .func(make_func<&soundex_impl>("soundex")
                  .returns(STRING)
                  .param(STRING)
                  .buffer_size(4)
                  .build())
        .func(make_func<&difference_impl>("difference")
                  .returns(INT)
                  .param(STRING)
                  .param(STRING)
                  .build())
        .func(make_func<&levenshtein_impl>("levenshtein")
                  .returns(INT)
                  .param(STRING)
                  .param(STRING)
                  .build())
        .func(make_func<&levenshtein_cost_impl>("levenshtein_cost")
                  .returns(INT)
                  .param(STRING)
                  .param(STRING)
                  .param(INT)
                  .param(INT)
                  .param(INT)
                  .build())
        .func(make_func<&levenshtein_less_equal_impl>("levenshtein_less_equal")
                  .returns(INT)
                  .param(STRING)
                  .param(STRING)
                  .param(INT)
                  .build())
        .func(make_func<&levenshtein_less_equal_cost_impl>(
                  "levenshtein_less_equal_cost")
                  .returns(INT)
                  .param(STRING)
                  .param(STRING)
                  .param(INT)
                  .param(INT)
                  .param(INT)
                  .param(INT)
                  .build())
        .func(make_func<&metaphone_impl>("metaphone")
                  .returns(STRING)
                  .param(STRING)
                  .param(INT)
                  .buffer_size(64)
                  .build())
        .func(make_func<&dmetaphone_impl>("dmetaphone")
                  .returns(STRING)
                  .param(STRING)
                  .buffer_size(7)
                  .build())
        .func(make_func<&dmetaphone_alt_impl>("dmetaphone_alt")
                  .returns(STRING)
                  .param(STRING)
                  .buffer_size(7)
                  .build()))

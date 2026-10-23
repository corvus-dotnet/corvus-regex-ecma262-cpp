// Runs an engine over the ECMA-262 regular expression test data (github.com/corvus-dotnet/ecma262-regex-test-data),
// judged by the data's own rule ("How a runner judges an engine" in its README) exactly as the C library's runner
// (tools/c/dataset.c in corvus-regex-ecma262-rs) judges it, of which this is a port:
//
//   dataset <data directory> <wrong-cases output file> [--engine corvus|std|boost|srell] [--files <filter>]
//           [--encodings utf16|utf8|both]
//
// With --engine corvus (the default) it judges this library, through corvus/regex_ecma262.hpp alone: is_match, find,
// captures, captures_all, find_all, replace, replace_all and split, over UTF-16 and UTF-8. The other engines, which a
// build with CRE_CPP_OTHER_ENGINES has, are judged over UTF-16 through their exec (see engines.hpp).
//
// Each record is asked about under each grammar V8 found it valid in: the record's flags with u added (the unicode
// answers), the record's flags alone (the legacy answers) and, in the file that has them, the flags with v added (the
// unicode_sets answers). A text is matched as UTF-16 and as UTF-8; a text with a lone surrogate has no UTF-8 form and
// is matched as UTF-16 alone. Records marked v8_deviation are left out. Any difference from V8 is a wrong answer; a
// refusal of a valid pattern, an acceptance of an invalid one, and a match that reaches its limit are counted apart and
// listed too. ill-formed-utf8.jsonl holds bytes that are not UTF-8, which the binding must refuse (errc::invalid_utf8).
// captures.jsonl is judged on exec, the global search, the sticky flag, replace and split. A match is given 2 seconds.
#include <corvus/regex_ecma262.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "engines.hpp"
#include "threads.hpp"

extern "C" {
#include "json.h"
}

namespace re = corvus::ecma262;
using dataset::result;
using dataset::unset;

namespace {

constexpr auto limit = std::chrono::milliseconds(2000);
constexpr int thread_count = 4;
const std::string replacement8 = "<$&|$1|$2|$10|$<a>|$$|$`|$'>";
const std::u16string replacement16 = u"<$&|$1|$2|$10|$<a>|$$|$`|$'>";

struct counts {
    long long right = 0, wrong = 0, refused = 0, accepted = 0, limited = 0, errors = 0;
    void add(const counts& b) {
        right += b.right;
        wrong += b.wrong;
        refused += b.refused;
        accepted += b.accepted;
        limited += b.limited;
        errors += b.errors;
    }
};

struct options {
    bool utf16 = true, utf8 = true;
    dataset::exec_engine* exec = nullptr;  // nullptr: this library
};

std::u16string_view units(const json* s) {
    return s ? std::u16string_view(reinterpret_cast<const char16_t*>(s->units), s->len) : std::u16string_view();
}

std::string utf8_lossy(std::u16string_view s) {
    size_t len;
    char* p = utf16_to_utf8_lossy(reinterpret_cast<const uint16_t*>(s.data()), s.size(), &len);
    std::string out(p, len);
    free(p);
    return out;
}

std::u16string utf16_of(std::string_view s) {
    size_t len;
    uint16_t* p = utf8_to_utf16(s.data(), s.size(), &len);
    std::u16string out(reinterpret_cast<const char16_t*>(p), len);
    free(p);
    return out;
}

bool lone_surrogate(std::u16string_view s) {
    return has_lone_surrogate(reinterpret_cast<const uint16_t*>(s.data()), s.size());
}

// The units shown in a wrong case: printable ASCII as it is, anything else as \uXXXX.
void escape(std::string& out, std::u16string_view u) {
    char buf[8];
    for (char16_t c : u) {
        if (c >= 0x20 && c < 0x7f && c != '\\') {
            out += static_cast<char>(c);
        } else {
            std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
            out += buf;
        }
    }
}

// The pattern as the Rust runner shows it: through UTF-8 (a lone surrogate as U+FFFD) and back, escaped.
std::string shown(std::u16string_view pattern) {
    std::string out;
    escape(out, utf16_of(utf8_lossy(pattern)));
    return out;
}

std::string first_line(std::string_view s) { return std::string(s.substr(0, s.find_first_of("\r\n"))); }

// For each UTF-16 code unit index of a text (and the index past its end), the byte offset in UTF-8; the low half of a
// pair is its character's third byte.
std::vector<size_t> byte_offsets(std::u16string_view s) {
    std::vector<size_t> at(s.size() + 1);
    size_t b = 0, k = 0;
    while (k < s.size()) {
        at[k] = b;
        char16_t c = s[k];
        if (c >= 0xD800 && c <= 0xDBFF && k + 1 < s.size() && s[k + 1] >= 0xDC00 && s[k + 1] <= 0xDFFF) {
            at[k + 1] = b + 2;
            b += 4;
            k += 2;
        } else {
            b += c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
            k += 1;
        }
    }
    at[s.size()] = b;
    return at;
}

// An exec answer: null, or [start, end] for the match then [start, end] or null for each group.
struct exec_answer {
    bool matched = false;
    std::vector<long> spans;  // 2 per group, -1 for a group that did not take part
    size_t groups() const { return spans.size() / 2; }
};

exec_answer exec_of(const json* e) {
    exec_answer a;
    if (!e || e->kind != JSON_ARRAY) return a;
    a.matched = true;
    a.spans.resize(e->len * 2);
    for (size_t k = 0; k < e->len; k++) {
        const json* g = &e->items[k];
        if (g->kind == JSON_ARRAY && g->len == 2) {
            a.spans[2 * k] = static_cast<long>(g->items[0].number);
            a.spans[2 * k + 1] = static_cast<long>(g->items[1].number);
        } else {
            a.spans[2 * k] = a.spans[2 * k + 1] = -1;
        }
    }
    return a;
}

// Whether the groups found (2 per group, unset for none) are the expected ones, positions mapped by map (none when
// empty).
bool same_slots(const exec_answer& e, bool found, const size_t* got, size_t got_groups, const std::vector<size_t>& map) {
    if (!e.matched || !found) return !e.matched && !found;
    if (e.groups() != got_groups) return false;
    for (size_t k = 0; k < got_groups; k++) {
        long s = e.spans[2 * k], t = e.spans[2 * k + 1];
        size_t gs = got[2 * k], gt = got[2 * k + 1];
        if (s < 0) {
            if (gs != unset) return false;
        } else {
            if (gs == unset) return false;
            size_t ms = map.empty() ? static_cast<size_t>(s) : map[s];
            size_t mt = map.empty() ? static_cast<size_t>(t) : map[t];
            if (ms != gs || mt != gt) return false;
        }
    }
    return true;
}

// Whether the pieces of a split are the expected array of strings and nulls.
template <class Piece>
bool same_split(const json* expected, const std::vector<Piece>& got) {
    if (!expected || expected->kind != JSON_ARRAY || expected->len != got.size()) return false;
    for (size_t k = 0; k < got.size(); k++) {
        const json* e = &expected->items[k];
        if (e->kind == JSON_NULL) {
            if (got[k]) return false;
        } else if (e->kind == JSON_STRING) {
            if (!got[k] || std::u16string_view(*got[k]) != units(e)) return false;
        } else {
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------------------------------------------------------
// Compiling, for either kind of engine

struct compiled {
    std::optional<re::regex> corvus;
    std::unique_ptr<dataset::exec_regex> exec;
    explicit operator bool() const { return corvus.has_value() || exec != nullptr; }
};

// The pattern as the Rust runner gives it to the crate: a lone surrogate as U+FFFD (the header reads UTF-16 patterns
// so). On a refusal, is_error for one that is not of the pattern, and the message.
compiled compile(const options& o, std::u16string_view pattern, const std::string& flags, bool& is_error,
                 std::string& message) {
    compiled c;
    is_error = false;
    message.clear();
    if (o.exec) {
        dataset::refusal why = dataset::refusal::error;
        std::string m;
        c.exec = o.exec->compile(pattern, flags, why, m);
        if (!c.exec) {
            is_error = why == dataset::refusal::error;
            message = (why == dataset::refusal::parse ? "PARSE: " : why == dataset::refusal::too_large ? "TOO-LARGE: "
                                                                                                         : "ERROR: ") +
                      m;
        }
        return c;
    }
    try {
        c.corvus.emplace(pattern, flags);
    } catch (const re::regex_error& e) {
        if (e.reason() == re::errc::invalid_pattern) {
            message = std::string("PARSE: ") + e.what();
        } else if (e.reason() == re::errc::pattern_too_large) {
            message = std::string("TOO-LARGE: ") + e.what();
        } else {
            is_error = true;
            message = std::string("ERROR: ") + e.what();
        }
    }
    return c;
}

// The answer of a match that throws: a limit (of time or of memory), or an error.
result failed(const re::regex_error& e, const char** why) {
    if (e.reason() == re::errc::invalid_utf8) return result::refused;
    if (e.reason() == re::errc::match_limit) {
        *why = std::strstr(e.what(), "memory") ? "memory" : "time";
        return result::limit;
    }
    *why = "panic";
    return result::error;
}

result test16(compiled& c, std::u16string_view text, const char** why) {
    if (c.exec) {
        return c.exec->exec(text, 0, nullptr, why);
    }
    try {
        return c.corvus->is_match(text, limit) ? result::yes : result::no;
    } catch (const re::regex_error& e) {
        return failed(e, why);
    }
}

result test8(compiled& c, std::string_view text, const char** why) {
    try {
        return c.corvus->is_match(text, limit) ? result::yes : result::no;
    } catch (const re::regex_error& e) {
        return failed(e, why);
    }
}

// ------------------------------------------------------------------------------------------------------------------
// The test answers

struct mode {
    const char* suffix;
    const char* answer;
    bool valid;
};

// The grammars the engine under test implements (all three for this library); one it does not is not asked: no
// verdict and no answer, as the test data's judging rule says ("The grammars an engine implements").
std::vector<std::string> asked_grammars = {"unicode", "legacy", "unicode_sets"};

bool asks(std::string_view grammar) {
    return std::find(asked_grammars.begin(), asked_grammars.end(), grammar) != asked_grammars.end();
}

std::vector<mode> modes_of(const json* r) {
    const json* valid = json_get(r, "valid");
    std::vector<mode> out;
    bool v = false;
    json_as_bool(json_get(valid, "unicode"), &v);
    if (asks("unicode")) out.push_back({"u", "unicode", v});
    v = false;
    json_as_bool(json_get(valid, "legacy"), &v);
    if (asks("legacy")) out.push_back({"", "legacy", v});
    if (const json* sets = json_get(valid, "unicode_sets")) {
        v = false;
        json_as_bool(sets, &v);
        if (asks("unicode_sets")) out.push_back({"v", "unicode_sets", v});
    }
    return out;
}

void judge_tests(const std::string& name, const json* r, std::u16string_view pattern, const options& o, counts& c,
                 std::vector<std::string>& w) {
    std::string flags = utf8_lossy(units(json_string(json_get(r, "flags"))));
    std::string pattern_shown = shown(pattern);
    const json* tests = json_get(r, "tests");
    for (const mode& m : modes_of(r)) {
        std::string mode_flags = flags + m.suffix;
        bool is_error;
        std::string message;
        compiled re = compile(o, pattern, mode_flags, is_error, message);
        std::string head = name + "\t/" + pattern_shown + "/" + mode_flags;
        if (!re) {
            if (is_error) c.errors++;
            if (m.valid) {
                c.refused++;
                w.push_back("REFUSED\t" + head + "\t" + first_line(message));
            }
            continue;
        }
        if (!m.valid) {
            c.accepted++;
            w.push_back("ACCEPTED-INVALID\t" + head);
            continue;
        }
        for (size_t t = 0; tests && tests->kind == JSON_ARRAY && t < tests->len; t++) {
            const json* test = &tests->items[t];
            bool expected;
            if (!json_as_bool(json_get(test, m.answer), &expected)) continue;
            const json* text_j = json_string(json_get(test, "text"));
            if (!text_j) continue;
            std::u16string_view text = units(text_j);
            for (int encoding = 0; encoding < 2; encoding++) {
                const char* why = "";
                result got;
                if (encoding == 0) {
                    if (!o.utf16) continue;
                    got = test16(re, text, &why);
                } else {
                    if (!o.utf8 || o.exec || lone_surrogate(text)) continue;
                    got = test8(re, utf8_lossy(text), &why);
                }
                const char* enc = encoding == 0 ? "utf16" : "utf8";
                std::string line;
                if (got == result::yes || got == result::no) {
                    if ((got == result::yes) == expected) {
                        c.right++;
                        continue;
                    }
                    c.wrong++;
                    line = "WRONG\t" + head + "\t";
                    escape(line, text);
                    line += std::string("\t") + enc + "\texpected " + (expected ? "true" : "false");
                } else if (got == result::error) {
                    c.errors++;
                    line = "ERROR\t" + head + "\t";
                    escape(line, text);
                    line += std::string("\t") + enc + "\t" + why;
                } else {
                    c.limited++;
                    line = "LIMIT\t" + head + "\t";
                    escape(line, text);
                    line += std::string("\t") + enc + "\t" + why;
                }
                w.push_back(std::move(line));
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------------------------
// captures.jsonl

struct judge {
    counts& c;
    std::vector<std::string>& w;
    std::string where;

    void check(const char* what, bool ok) {
        if (ok) {
            c.right++;
            return;
        }
        c.wrong++;
        w.push_back("WRONG\tcaptures.jsonl\t" + where + "\t" + what);
    }
};

// The groups of a captures object as spans, unset for a group that did not take part.
template <class C>
size_t groups_of(const re::basic_captures<C>& caps, std::vector<size_t>& spans) {
    size_t n = caps.size();
    spans.resize(std::max(spans.size(), 2 * n));
    for (size_t k = 0; k < n; k++) {
        re::match m = caps[k];
        spans[2 * k] = m.start;
        spans[2 * k + 1] = m.end;
    }
    return n;
}

// The global search's matches against all (at most 20 of them), positions mapped by map: the Rust runner's judge_all.
template <class Range>
bool judge_all(Range&& matches, const std::vector<exec_answer>& all, const std::vector<size_t>& map) {
    size_t n = 0;
    for (re::match m : matches) {
        // One match more than the data has: wrong, unless the data kept only the first twenty.
        if (n >= all.size()) return all.size() == 20;
        if (!all[n].matched) return false;
        long s = all[n].spans[0], t = all[n].spans[1];
        size_t ms = map.empty() ? static_cast<size_t>(s) : map[s];
        size_t mt = map.empty() ? static_cast<size_t>(t) : map[t];
        if (ms != m.start || mt != m.end) return false;
        n++;
    }
    return !((all.size() < 20 && n != all.size()) || (all.size() == 20 && n < 20));
}

void judge_rest_corvus(judge& j, const json* a, std::u16string_view text, const re::regex& re, const re::regex& g,
                       const re::regex& y, const exec_answer& exec, bool lone, const std::string& utf8,
                       const std::vector<size_t>& byte_at, std::vector<size_t>& spans);

void judge_mode_corvus(judge& j, const json* a, std::u16string_view text, const re::regex& re, const re::regex& g,
                       const re::regex& y) {
    const std::vector<size_t> none;
    bool lone = lone_surrogate(text);
    std::string utf8 = lone ? std::string() : utf8_lossy(text);
    std::vector<size_t> byte_at = lone ? std::vector<size_t>() : byte_offsets(text);
    std::vector<size_t> spans(2 * (re.group_count() + 1));

    // exec
    exec_answer exec = exec_of(json_get(a, "exec"));
    try {
        auto caps = re.captures(text, 0, limit);
        j.check("exec", caps ? same_slots(exec, true, spans.data(), groups_of(*caps, spans), none)
                             : same_slots(exec, false, nullptr, 0, none));
    } catch (const re::regex_error& e) {
        if (e.reason() == re::errc::match_limit) {
            j.c.limited++;
            j.w.push_back("LIMIT\tcaptures.jsonl\t" + j.where);
        } else {
            j.c.errors++;
            j.w.push_back("ERROR\tcaptures.jsonl\t" + j.where + "\t" + e.code().message());
        }
        return;
    }
    // Past the first answer the matches have no limit, as in the C runner; anything they throw is an error.
    try {
        judge_rest_corvus(j, a, text, re, g, y, exec, lone, utf8, byte_at, spans);
    } catch (const re::regex_error& e) {
        j.c.errors++;
        j.w.push_back("ERROR\tcaptures.jsonl\t" + j.where + "\t" + e.code().message());
    }
}

void judge_rest_corvus(judge& j, const json* a, std::u16string_view text, const re::regex& re, const re::regex& g,
                       const re::regex& y, const exec_answer& exec, bool lone, const std::string& utf8,
                       const std::vector<size_t>& byte_at, std::vector<size_t>& spans) {
    const std::vector<size_t> none;
    if (!lone) {
        auto caps = re.captures(utf8);
        j.check("exec-utf8", caps ? same_slots(exec, true, spans.data(), groups_of(*caps, spans), byte_at)
                                  : same_slots(exec, false, nullptr, 0, byte_at));
    }

    // all
    {
        const json* all_j = json_get(a, "all");
        std::vector<exec_answer> all;
        for (size_t k = 0; all_j && all_j->kind == JSON_ARRAY && k < all_j->len; k++) all.push_back(exec_of(&all_j->items[k]));
        // The groups of each match too, over UTF-16.
        bool all_ok = true;
        size_t n = 0;
        for (const re::u16captures& caps : g.captures_all(text)) {
            if (n >= all.size()) {
                all_ok = all_ok && all.size() == 20;
                break;
            }
            if (!same_slots(all[n], true, spans.data(), groups_of(caps, spans), none)) {
                all_ok = false;
                break;
            }
            n++;
        }
        if ((all.size() < 20 && n != all.size()) || (all.size() == 20 && n < 20)) all_ok = false;
        j.check("all", all_ok && judge_all(g.find_all(text), all, none));
        if (!lone) j.check("all-utf8", judge_all(g.find_all(utf8), all, byte_at));
    }

    // sticky at 1
    if (!text.empty()) {
        exec_answer sticky = exec_of(json_get(a, "sticky"));
        auto caps = y.captures(text, 1);
        j.check("sticky", caps ? same_slots(sticky, true, spans.data(), groups_of(*caps, spans), none)
                               : same_slots(sticky, false, nullptr, 0, none));
        if (!lone && (byte_at[1] >= utf8.size() || (static_cast<unsigned char>(utf8[byte_at[1]]) & 0xC0) != 0x80)) {
            auto caps8 = y.captures(utf8, byte_at[1]);
            j.check("sticky-utf8", caps8 ? same_slots(sticky, true, spans.data(), groups_of(*caps8, spans), byte_at)
                                         : same_slots(sticky, false, nullptr, 0, byte_at));
        }
    }

    // replace
    {
        const json* replace = json_string(json_get(a, "replace"));
        const json* replace_all = json_string(json_get(a, "replaceAll"));
        j.check("replace", replace && re.replace(text, replacement16) == units(replace));
        j.check("replaceAll", replace_all && g.replace(text, replacement16) == units(replace_all));
        if (!lone) {
            // Over UTF-8 a half character is written as U+FFFD, which is what a lossy conversion of V8's answer
            // writes for it.
            j.check("replace-utf8", replace && re.replace(utf8, replacement8) == utf8_lossy(units(replace)));
            j.check("replaceAll-utf8",
                    replace_all && g.replace(utf8, replacement8) == utf8_lossy(units(replace_all)));
        }
    }

    // split
    j.check("split", same_split(json_get(a, "split"), re.split(text)));
    j.check("split3", same_split(json_get(a, "split3"), re.split(text, 3)));
}

// After an empty match at pos, the next position: a code point with the u or v flag, a code unit without.
size_t advance16(std::u16string_view text, size_t pos, bool unicode) {
    if (unicode && pos + 1 < text.size() && text[pos] >= 0xD800 && text[pos] <= 0xDBFF && text[pos + 1] >= 0xDC00 &&
        text[pos + 1] <= 0xDFFF) {
        return pos + 2;
    }
    return pos + 1;
}

// GetSubstitution over UTF-16, as the Rust runner writes it for regress.
void substitute16(std::u16string& out, std::u16string_view text, const size_t* groups, const dataset::exec_regex& re,
                  std::u16string_view tpl) {
    size_t from = groups[0], to = groups[1];
    size_t m = re.groups() - 1;
    auto digit = [&](size_t i) -> long { return i < tpl.size() && tpl[i] >= '0' && tpl[i] <= '9' ? tpl[i] - '0' : -1; };
    auto group = [&](size_t g) {
        if (groups[2 * g] != unset) out.append(text.substr(groups[2 * g], groups[2 * g + 1] - groups[2 * g]));
    };
    size_t k = 0;
    while (k < tpl.size()) {
        if (tpl[k] != '$' || k + 1 >= tpl.size()) {
            out += tpl[k++];
            continue;
        }
        char16_t c = tpl[k + 1];
        if (c == '$') {
            out += u'$';
            k += 2;
        } else if (c == '&') {
            out.append(text.substr(from, to - from));
            k += 2;
        } else if (c == '`') {
            out.append(text.substr(0, from));
            k += 2;
        } else if (c == '\'') {
            out.append(text.substr(to));
            k += 2;
        } else if (c >= '0' && c <= '9') {
            long d1 = digit(k + 1), d2 = digit(k + 2);
            if (d2 >= 0 && d1 * 10 + d2 >= 1 && static_cast<size_t>(d1 * 10 + d2) <= m) {
                group(static_cast<size_t>(d1 * 10 + d2));
                k += 3;
            } else if (d1 >= 1 && static_cast<size_t>(d1) <= m) {
                group(static_cast<size_t>(d1));
                k += 2;
            } else {
                out += tpl[k++];
            }
        } else if (c == '<') {
            size_t close = tpl.find(u'>', k + 2);
            bool any_name = false;
            for (size_t g = 1; g <= m && !any_name; g++) any_name = !re.name(g).empty();
            if (close != std::u16string_view::npos && any_name) {
                std::u16string_view wanted = tpl.substr(k + 2, close - (k + 2));
                for (size_t g = 1; g <= m; g++) {
                    if (re.name(g) == wanted && groups[2 * g] != unset) {
                        group(g);
                        break;
                    }
                }
                k = close + 1;
            } else {
                out += tpl[k++];
            }
        } else {
            out += tpl[k++];
        }
    }
}

void judge_mode_exec(judge& j, const json* a, std::u16string_view text, dataset::exec_regex& re,
                     dataset::exec_regex& y) {
    const std::vector<size_t> none;
    size_t n = re.groups();
    std::vector<size_t> groups(2 * n);
    const char* why = "";
    bool unicode = re.unicode();
    // An answer never given (a limit or an error) leaves every answer of the test and grammar after it unanswered too,
    // since each is built on the matches before it.
    result failure = result::no;
    long long before = j.c.right + j.c.wrong;
    auto exec = [&](dataset::exec_regex& r, size_t from, size_t* out) -> int {
        if (from > text.size()) return 0;
        result got = r.exec(text, from, out, &why);
        if (got == result::limit || got == result::error) {
            failure = got;
            return -1;
        }
        return got == result::yes ? 1 : 0;
    };

    // The answers in order, a lambda so that the first answer never given ends them (MSVC reads a lambda called where
    // it is written, at the start of a statement, as an attribute, so it is named first).
    auto answer_all = [&] {
        exec_answer want = exec_of(json_get(a, "exec"));
        int rc = exec(re, 0, groups.data());
        if (rc < 0) return;
        j.check("exec", same_slots(want, rc == 1, groups.data(), n, none));

        // The global search.
        {
            const json* all_j = json_get(a, "all");
            size_t all_len = all_j && all_j->kind == JSON_ARRAY ? all_j->len : 0;
            size_t found = 0, pos = 0;
            bool ok = true;
            while (found < 20) {
                rc = exec(re, pos, groups.data());
                if (rc < 0) return;
                if (rc != 1) break;
                size_t s = groups[0], e = groups[1];
                pos = s == e ? advance16(text, e, unicode) : e;
                if (found < all_len && !same_slots(exec_of(&all_j->items[found]), true, groups.data(), n, none)) ok = false;
                found++;
            }
            j.check("all", (all_len < 20 ? found == all_len : found >= 20) && ok);
        }

        // sticky at 1, by the y flag of the engine.
        if (!text.empty()) {
            exec_answer sticky = exec_of(json_get(a, "sticky"));
            std::vector<size_t> ygroups(2 * y.groups());
            rc = exec(y, 1, ygroups.data());
            if (rc < 0) return;
            j.check("sticky", same_slots(sticky, rc == 1, ygroups.data(), y.groups(), none));
        }

        // replace and replaceAll.
        for (int global = 0; global < 2; global++) {
            std::u16string out;
            size_t last = 0, pos = 0;
            for (;;) {
                rc = exec(re, pos, groups.data());
                if (rc < 0) return;
                if (rc != 1) break;
                size_t s = groups[0], e = groups[1];
                out.append(text.substr(last, s - last));
                substitute16(out, text, groups.data(), re, replacement16);
                last = e;
                if (!global) break;
                pos = s == e ? advance16(text, e, unicode) : e;
            }
            out.append(text.substr(last));
            const json* want_j = json_string(json_get(a, global ? "replaceAll" : "replace"));
            j.check(global ? "replaceAll" : "replace", want_j && units(want_j) == out);
        }

        // split and split3.
        for (int k = 0; k < 2; k++) {
            size_t most = k == 0 ? 0xFFFFFFFFu : 3;
            std::vector<std::optional<std::u16string_view>> out;
            if (text.empty()) {
                rc = exec(re, 0, groups.data());
                if (rc < 0) return;
                if (rc != 1) out.emplace_back(text);
            } else {
                size_t p = 0, q = 0;
                bool stop = false;
                while (q < text.size() && !stop) {
                    rc = exec(re, q, groups.data());
                    if (rc < 0) return;
                    if (rc != 1) break;
                    size_t s = groups[0], e = groups[1];
                    if (s >= text.size()) break;
                    size_t end = std::min(e, text.size());
                    if (end == p) {
                        q = advance16(text, s, unicode);
                        continue;
                    }
                    out.emplace_back(text.substr(p, s - p));
                    if (out.size() == most) {
                        stop = true;
                        break;
                    }
                    p = end;
                    for (size_t gi = 1; gi < n; gi++) {
                        if (groups[2 * gi] == unset) {
                            out.emplace_back();
                        } else {
                            out.emplace_back(text.substr(groups[2 * gi], groups[2 * gi + 1] - groups[2 * gi]));
                        }
                        if (out.size() == most) {
                            stop = true;
                            break;
                        }
                    }
                    q = p;
                }
                if (out.size() < most) out.emplace_back(text.substr(p));
            }
            j.check(k == 0 ? "split" : "split3", same_split(json_get(a, k == 0 ? "split" : "split3"), out));
        }
    };
    answer_all();

    if (failure == result::limit || failure == result::error) {
        // Each of the seven answers (six for an empty text, which has no sticky answer) not given.
        long long missing = (text.empty() ? 6 : 7) - (j.c.right + j.c.wrong - before);
        if (failure == result::limit) {
            j.c.limited += missing;
            j.w.push_back("LIMIT\tcaptures.jsonl\t" + j.where + "\t" + why);
        } else {
            j.c.errors += missing;
            j.w.push_back("ERROR\tcaptures.jsonl\t" + j.where + "\t" + why);
        }
    }
}

void judge_captures(const json* r, std::u16string_view pattern, const options& o, counts& c,
                    std::vector<std::string>& w) {
    std::string flags = utf8_lossy(units(json_string(json_get(r, "flags"))));
    const json* valid = json_get(r, "valid");
    const json* tests = json_get(r, "tests");
    std::string pattern_shown = shown(pattern);
    // The compiled forms of the record's pattern, by flags, as the Rust runner caches them; a deque, so that a
    // reference to one stays valid as the others are added.
    std::deque<std::pair<std::string, compiled>> cache;
    auto make = [&](const std::string& f) -> compiled& {
        for (auto& [k, v] : cache) {
            if (k == f) return v;
        }
        bool is_error;
        std::string message;
        cache.emplace_back(f, compile(o, pattern, f, is_error, message));
        return cache.back().second;
    };
    for (size_t t = 0; tests && tests->kind == JSON_ARRAY && t < tests->len; t++) {
        const json* test = &tests->items[t];
        if (json_is_true(json_get(test, "v8_deviation"))) continue;
        const json* text_j = json_string(json_get(test, "text"));
        if (!text_j) continue;
        std::u16string_view text = units(text_j);
        for (int m = 0; m < 2; m++) {
            const char* mode_name = m == 0 ? "unicode" : "legacy";
            if (!asks(mode_name) || !json_is_true(json_get(valid, mode_name))) continue;
            const json* a = json_get(test, mode_name);
            if (!a) continue;
            std::string mode_flags = flags + (m == 0 ? "u" : "");
            judge j{c, w, "/" + pattern_shown + "/" + mode_flags + "\t"};
            escape(j.where, text);
            compiled& re = make(mode_flags);
            compiled& g = make(mode_flags + "g");
            compiled& y = make(mode_flags + "y");
            if (!re || !g || !y) {
                c.refused++;
                w.push_back("REFUSED\tcaptures.jsonl\t" + j.where);
            } else if (o.exec) {
                judge_mode_exec(j, a, text, *re.exec, *y.exec);
            } else {
                judge_mode_corvus(j, a, text, *re.corvus, *g.corvus, *y.corvus);
            }
        }
    }
}

// A record of ill-formed-utf8.jsonl: under each grammar the pattern is valid in, each test's bytes, which are not
// UTF-8, are one answer over UTF-8, and the right one is a refusal (errc::invalid_utf8). An engine with no UTF-8
// interface is asked nothing.
void judge_ill_formed(const json* r, std::u16string_view pattern, const options& o, counts& c,
                      std::vector<std::string>& w) {
    if (!o.utf8 || o.exec) return;
    const std::string name = "ill-formed-utf8.jsonl";
    std::string flags = utf8_lossy(units(json_string(json_get(r, "flags"))));
    std::string pattern_shown = shown(pattern);
    const json* tests = json_get(r, "tests");
    for (const mode& m : modes_of(r)) {
        std::string mode_flags = flags + m.suffix;
        bool is_error;
        std::string message;
        compiled re = compile(o, pattern, mode_flags, is_error, message);
        std::string head = name + "\t/" + pattern_shown + "/" + mode_flags;
        if (!re) {
            if (is_error) c.errors++;
            if (m.valid) {
                c.refused++;
                w.push_back("REFUSED\t" + head + "\t" + first_line(message));
            }
            continue;
        }
        if (!m.valid) {
            c.accepted++;
            w.push_back("ACCEPTED-INVALID\t" + head);
            continue;
        }
        for (size_t t = 0; tests && tests->kind == JSON_ARRAY && t < tests->len; t++) {
            const json* hex_j = json_string(json_get(&tests->items[t], "bytes"));
            if (!hex_j) continue;
            std::string hex = utf8_lossy(units(hex_j));
            std::string bytes;
            for (size_t k = 0; k + 1 < hex.size(); k += 2) {
                bytes.push_back(static_cast<char>(std::stoi(hex.substr(k, 2), nullptr, 16)));
            }
            const char* why = "";
            result got = test8(re, bytes, &why);
            if (got == result::refused) {
                c.right++;
                continue;
            }
            std::string line;
            if (got == result::yes || got == result::no) {
                c.wrong++;
                line = "WRONG\t" + head + "\t" + hex + "\tutf8\tread as UTF-8 (" +
                       (got == result::yes ? "matched" : "no match") + "), not refused";
            } else if (got == result::error) {
                c.errors++;
                line = "ERROR\t" + head + "\t" + hex + "\tutf8\t" + why;
            } else {
                c.limited++;
                line = "LIMIT\t" + head + "\t" + hex + "\tutf8\t" + why;
            }
            w.push_back(std::move(line));
        }
    }
}

void judge_record(const std::string& name, const json* r, const options& o, counts& c, std::vector<std::string>& w) {
    const json* pattern = json_string(json_get(r, "pattern"));
    if (!pattern) return;
    if (name == "captures.jsonl") {
        if (o.utf16) judge_captures(r, units(pattern), o, c, w);
        return;
    }
    if (name == "ill-formed-utf8.jsonl") {
        judge_ill_formed(r, units(pattern), o, c, w);
        return;
    }
    judge_tests(name, r, units(pattern), o, c, w);
}

// ------------------------------------------------------------------------------------------------------------------
// The run

// Whether a file name matches a filter with * wildcards.
bool glob(std::string_view pattern, std::string_view name) {
    size_t star = pattern.find('*');
    if (star == std::string_view::npos) return pattern == name;
    if (name.substr(0, star) != pattern.substr(0, star)) return false;
    std::string_view rest = name.substr(star);
    std::string_view p = pattern.substr(star + 1);
    for (;;) {
        size_t next = p.find('*');
        if (next == std::string_view::npos) return rest.size() >= p.size() && rest.substr(rest.size() - p.size()) == p;
        size_t at = rest.find(p.substr(0, next));
        if (at == std::string_view::npos) return false;
        rest = rest.substr(at + next);
        p = p.substr(next + 1);
    }
}


// ------------------------------------------------------------------------------------------------------------------
// Running an engine with no bound of its own in child processes (POSIX)
//
// An engine that has no limit of time can backtrack for hours on one record, and one that recurses can overflow its
// stack. So with --isolate each worker's records run in a child process (`dataset --child ...`), which reports each
// record as it starts and ends; a record that has not ended within --record-seconds, or whose child stops, is counted
// as unanswered, every answer it asks, and its child is replaced by one that goes on from the next record.

// The answers a record asks of an engine over UTF-16 alone: what an unanswered record counts as reaching a limit.
long long answers_asked(const std::string& name, const json* r) {
    long long n = 0;
    const json* tests = json_get(r, "tests");
    if (name == "captures.jsonl") {
        const json* valid = json_get(r, "valid");
        for (size_t t = 0; tests && tests->kind == JSON_ARRAY && t < tests->len; t++) {
            const json* test = &tests->items[t];
            if (json_is_true(json_get(test, "v8_deviation"))) continue;
            const json* text = json_string(json_get(test, "text"));
            if (!text) continue;
            for (const char* m : {"unicode", "legacy"}) {
                if (asks(m) && json_is_true(json_get(valid, m)) && json_get(test, m)) n += text->len == 0 ? 6 : 7;
            }
        }
        return n;
    }
    for (const mode& m : modes_of(r)) {
        if (!m.valid) continue;
        for (size_t t = 0; tests && tests->kind == JSON_ARRAY && t < tests->len; t++) {
            bool expected;
            if (json_as_bool(json_get(&tests->items[t], m.answer), &expected) &&
                json_string(json_get(&tests->items[t], "text"))) {
                n++;
            }
        }
    }
    return n;
}

// The child: records first to last of one file, each reported as R <index>, its wrong cases as W lines, and its
// counts as C <right> <wrong> <refused> <accepted> <limited> <errors> (S for a record left out); D at the end.
int run_child(const std::string& directory, const std::string& name, const options& o, size_t first, size_t last) {
    std::ifstream in(std::filesystem::path(directory) / name, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t index = 0;
    for (size_t p = 0; p < text.size() && index < last; index++) {
        size_t end = text.find('\n', p);
        if (end == std::string::npos) end = text.size();
        size_t n = end - p;
        if (n > 0 && text[p + n - 1] == '\r') n--;
        if (index >= first) {
            std::printf("R %zu\n", index);
            std::fflush(stdout);
            json_arena* arena;
            const char* error;
            json* r = json_parse(text.data() + p, n, &arena, &error);
            if (!r) {
                std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), index + 1, error);
                return 2;
            }
            if (json_is_true(json_get(r, "v8_deviation"))) {
                std::printf("S\n");
            } else {
                counts c;
                std::vector<std::string> w;
                judge_record(name, r, o, c, w);
                for (const auto& line : w) std::printf("W %s\n", line.c_str());
                std::printf("C %lld %lld %lld %lld %lld %lld\n", c.right, c.wrong, c.refused, c.accepted, c.limited,
                            c.errors);
            }
            std::fflush(stdout);
            json_arena_free(arena);
        }
        p = end + 1;
    }
    std::printf("D\n");
    std::fflush(stdout);
    return 0;
}

#ifndef _WIN32
// The parent's side of one worker: records first to last of a file, through as many children as it takes.
void supervise(const std::vector<std::string>& child_args, const std::string& name,
               const std::vector<std::string_view>& lines, size_t first, size_t last, double record_seconds,
               counts& c, std::vector<std::string>& w, long long& records, long long& skipped) {
    size_t start = first;
    while (start < last) {
        int fd[2];
        if (pipe(fd) != 0) throw std::runtime_error("pipe");
        std::vector<std::string> args = child_args;
        args.push_back(std::to_string(start));
        args.push_back(std::to_string(last));
        pid_t pid = fork();
        if (pid == 0) {
            dup2(fd[1], 1);
            close(fd[0]);
            close(fd[1]);
            std::vector<char*> argv;
            for (auto& a : args) argv.push_back(a.data());
            argv.push_back(nullptr);
            execv(argv[0], argv.data());
            _exit(127);
        }
        close(fd[1]);
        size_t current = unset, next_start = start;
        bool done = false, timed_out = false;
        std::vector<std::string> pending;
        std::string buffer;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(record_seconds);
        for (;;) {
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (current != unset && left.count() <= 0) {
                timed_out = true;
                break;
            }
            pollfd p{fd[0], POLLIN, 0};
            int ready = poll(&p, 1, current != unset ? static_cast<int>(left.count()) : 1000);
            if (ready == 0) continue;
            char chunk[65536];
            ssize_t got = read(fd[0], chunk, sizeof chunk);
            if (got <= 0) break;
            buffer.append(chunk, static_cast<size_t>(got));
            size_t at;
            while ((at = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, at);
                buffer.erase(0, at + 1);
                if (line.starts_with("R ")) {
                    current = std::stoull(line.substr(2));
                    deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(record_seconds);
                } else if (line.starts_with("W ")) {
                    pending.push_back(line.substr(2));
                } else if (line == "S") {
                    skipped++;
                    next_start = current + 1;
                    current = unset;
                } else if (line.starts_with("C ")) {
                    counts r;
                    std::sscanf(line.c_str() + 2, "%lld %lld %lld %lld %lld %lld", &r.right, &r.wrong, &r.refused,
                                &r.accepted, &r.limited, &r.errors);
                    c.add(r);
                    for (auto& l : pending) w.push_back(std::move(l));
                    pending.clear();
                    records++;
                    next_start = current + 1;
                    current = unset;
                } else if (line == "D") {
                    done = true;
                }
            }
            if (done) break;
        }
        if (!done) kill(pid, SIGKILL);
        close(fd[0]);
        int status = 0;
        waitpid(pid, &status, 0);
        if (done) break;
        // The record in progress gave no answer: each answer it asks is counted as reaching a limit.
        size_t lost = current != unset ? current : next_start;
        if (lost >= last) break;
        json_arena* arena;
        const char* error;
        json* r = json_parse(lines[lost].data(), lines[lost].size(), &arena, &error);
        if (r && json_is_true(json_get(r, "v8_deviation"))) {
            skipped++;
        } else if (r) {
            records++;
            c.limited += answers_asked(name, r);
            char what[64];
            std::snprintf(what, sizeof what, timed_out ? "no answer in %.0f s" : "the engine stopped", record_seconds);
            w.push_back("LIMIT\t" + name + "\trecord " + std::to_string(lost + 1) + "\t" + what);
        }
        if (r) json_arena_free(arena);
        start = lost + 1;
    }
}
#endif

}  // namespace

int main(int argc, char** argv) {
    // dataset --child <directory> <file> <engine> <encodings> <first> <last>: one worker of an isolated run.
    if (argc == 8 && std::string_view(argv[1]) == "--child") {
        options o;
        std::string enc = argv[5];
        o.utf16 = enc == "both" || enc == "utf16";
        o.utf8 = enc == "both" || enc == "utf8";
        std::unique_ptr<dataset::exec_engine> exec;
        if (std::string_view(argv[4]) != "corvus") {
            exec = dataset::make_exec_engine(argv[4]);
            if (exec) asked_grammars = exec->grammars();
            if (!exec) return 2;
            o.exec = exec.get();
        }
        int status = 2;
        dataset::thread t([&] { status = run_child(argv[2], argv[3], o, std::stoull(argv[6]), std::stoull(argv[7])); },
                          std::size_t(256) << 20);
        t.join();
        return status;
    }
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: %s <data directory> <wrong-cases file> [--engine corvus|...] [--files <filter>] "
                     "[--encodings utf16|utf8|both] [--isolate on [--record-seconds 10]]\n",
                     argv[0]);
        return 2;
    }
    std::string directory = argv[1], wrong_path = argv[2], filter = "*.jsonl", encodings = "both", engine = "corvus";
    bool isolate = false;
    double record_seconds = 10;
    for (int k = 3; k + 1 < argc; k += 2) {
        std::string_view flag = argv[k];
        if (flag == "--isolate") {
            isolate = std::string_view(argv[k + 1]) == "on";
        } else if (flag == "--record-seconds") {
            record_seconds = std::stod(argv[k + 1]);
        } else if (flag == "--engine") {
            engine = argv[k + 1];
        } else if (flag == "--files") {
            filter = argv[k + 1];
        } else if (flag == "--encodings") {
            encodings = argv[k + 1];
        } else {
            std::fprintf(stderr, "unknown option %s\n", argv[k]);
            return 2;
        }
    }
    options o;
    o.utf16 = encodings == "both" || encodings == "utf16";
    o.utf8 = encodings == "both" || encodings == "utf8";
    std::unique_ptr<dataset::exec_engine> exec;
    if (engine != "corvus") {
        exec = dataset::make_exec_engine(engine);
        if (exec) asked_grammars = exec->grammars();
        if (!exec) {
            std::string names;
            for (const auto& n : dataset::exec_engine_names()) names += " " + n;
            std::fprintf(stderr, "no engine %s in this build (corvus%s)\n", engine.c_str(), names.c_str());
            return 2;
        }
        o.exec = exec.get();
    }

#ifndef _WIN32
    // This program, for the children of an isolated run, read once: a later rebuild replaces the file.
    std::string self = isolate ? std::filesystem::canonical("/proc/self/exe").string() : std::string();
#endif
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        std::string name = entry.path().filename().string();
        if (name.size() > 6 && name.ends_with(".jsonl") && glob(filter, name)) files.push_back(name);
    }
    std::sort(files.begin(), files.end());

    counts total;
    std::vector<std::string> wrong;
    long long records = 0, skipped = 0;
    auto start = std::chrono::steady_clock::now();
    auto seconds = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
    for (const std::string& name : files) {
        std::ifstream in(std::filesystem::path(directory) / name, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "cannot read %s\n", name.c_str());
            return 2;
        }
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<std::string_view> lines;
        for (size_t p = 0; p < text.size();) {
            size_t end = text.find('\n', p);
            if (end == std::string::npos) end = text.size();
            size_t n = end - p;
            if (n > 0 && text[p + n - 1] == '\r') n--;
            lines.emplace_back(text.data() + p, n);
            p = end + 1;
        }
        std::atomic<size_t> next{0};
        std::mutex lock;
        counts file_total;
        long long file_records = 0, file_skipped = 0;
        auto worker = [&] {
            counts c;
            std::vector<std::string> w;
            long long rec = 0, skip = 0;
            for (;;) {
                size_t k = next++;
                if (k >= lines.size()) break;
                json_arena* arena;
                const char* error;
                json* r = json_parse(lines[k].data(), lines[k].size(), &arena, &error);
                if (!r) {
                    std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), k + 1, error);
                    std::exit(2);
                }
                if (json_is_true(json_get(r, "v8_deviation"))) {
                    skip++;
                } else {
                    rec++;
                    judge_record(name, r, o, c, w);
                }
                json_arena_free(arena);
            }
            std::lock_guard<std::mutex> g(lock);
            file_total.add(c);
            wrong.insert(wrong.end(), std::make_move_iterator(w.begin()), std::make_move_iterator(w.end()));
            file_records += rec;
            file_skipped += skip;
        };
        if (isolate) {
#ifdef _WIN32
            std::fprintf(stderr, "--isolate is POSIX only\n");
            return 2;
#else
            // Each worker a contiguous share of the records, through child processes of this program.
            std::vector<std::string> child_args{self, "--child", directory, name, engine, encodings};
            std::vector<std::thread> supervisors;
            for (int t = 0; t < thread_count; t++) {
                size_t first = lines.size() * t / thread_count, last = lines.size() * (t + 1) / thread_count;
                supervisors.emplace_back([&, first, last] {
                    counts c;
                    std::vector<std::string> w;
                    long long rec = 0, skip = 0;
                    supervise(child_args, name, lines, first, last, record_seconds, c, w, rec, skip);
                    std::lock_guard<std::mutex> g(lock);
                    file_total.add(c);
                    wrong.insert(wrong.end(), std::make_move_iterator(w.begin()), std::make_move_iterator(w.end()));
                    file_records += rec;
                    file_skipped += skip;
                });
            }
            for (auto& t : supervisors) t.join();
#endif
        } else {
            // A deep pattern needs a deep stack in an engine that recurses.
            std::vector<dataset::thread> threads;
            for (int t = 0; t < thread_count; t++) threads.emplace_back(worker, std::size_t(256) << 20);
            for (auto& t : threads) t.join();
        }
        std::printf("%-24s right %10lld  wrong %6lld  refused-valid %4lld  accepted-invalid %4lld  limit %4lld  %6.1fs\n",
                    name.c_str(), file_total.right, file_total.wrong, file_total.refused, file_total.accepted,
                    file_total.limited, seconds());
        std::fflush(stdout);
        total.add(file_total);
        records += file_records;
        skipped += file_skipped;
    }
    std::sort(wrong.begin(), wrong.end());
    if (FILE* out = std::fopen(wrong_path.c_str(), "wb")) {
        for (const auto& line : wrong) std::fprintf(out, "%s\n", line.c_str());
        std::fclose(out);
    }
    std::printf(
        "\nrecords %lld (skipped %lld deviations)  right %lld  wrong %lld  refused-valid %lld  accepted-invalid %lld  "
        "limit %lld  errors %lld  in %.1fs\n",
        records, skipped, total.right, total.wrong, total.refused, total.accepted, total.limited, total.errors,
        seconds());
    return total.wrong + total.refused + total.accepted + total.errors + total.limited == 0 ? 0 : 1;
}

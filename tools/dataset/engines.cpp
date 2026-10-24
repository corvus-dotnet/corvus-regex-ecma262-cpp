// The engines with exec alone that the data-set runner judges, each read the way it is documented to read ECMAScript:
//
// - std (std::regex with std::regex_constants::ECMAScript, the standard's modified ECMAScript 3 grammar), over
//   wchar_t;
// - boost (Boost.Regex, whose ECMAScript option is its Perl syntax), over wchar_t, with match_single_line unless m
//   and no_mod_s unless s, so that ^, $ and . read as ECMA-262 reads them;
// - srell (SRELL, an ECMAScript library), over char16_t (UTF-16, u16regex) with u or v, and over char32_t one unit
//   to an element without, so that it matches by code unit as ECMA-262 does without u.
//
// For std and boost the text is given one UTF-16 unit to a wchar_t without u or v, and one code point to a wchar_t
// with them (where wchar_t is 32 bits); positions are mapped back to UTF-16 units. A flag an engine has no way to
// honour (s for std, v for std and boost) is a refusal. None of them has a limit of time; Boost and SRELL stop a match
// whose backtracking passes their own bound (error_complexity), which is counted as a limit, as is a std::regex
// error_complexity or error_stack.
#include "engines.hpp"

#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef CRE_CPP_HAVE_BOOST
#define BOOST_REGEX_STANDALONE
#include <boost/regex.hpp>
#endif

#ifdef CRE_CPP_HAVE_SRELL
#include "srell.hpp"
#endif

namespace dataset {
namespace {

bool has(std::string_view flags, char f) { return flags.find(f) != std::string_view::npos; }

// The names of a pattern's capturing groups, by group number (1 and up; empty for a group with no name), read from the
// pattern: the engines here give a group's number for a name, but not the names.
std::vector<std::u16string> group_names(std::u16string_view p, bool sets) {
    std::vector<std::u16string> names(1);
    int class_depth = 0;
    for (std::size_t i = 0; i < p.size(); i++) {
        char16_t c = p[i];
        if (c == '\\') {
            i++;
        } else if (class_depth > 0) {
            if (c == ']') {
                class_depth--;
            } else if (c == '[' && sets) {
                class_depth++;
            }
        } else if (c == '[') {
            class_depth = 1;
        } else if (c == '(') {
            if (i + 1 < p.size() && p[i + 1] == '?') {
                if (i + 2 < p.size() && p[i + 2] == '<' && i + 3 < p.size() && p[i + 3] != '=' && p[i + 3] != '!') {
                    std::size_t close = p.find(u'>', i + 3);
                    names.emplace_back(p.substr(i + 3, close == std::u16string_view::npos ? 0 : close - (i + 3)));
                }
            } else {
                names.emplace_back();
            }
        }
    }
    return names;
}

bool high(char16_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool low(char16_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

// A UTF-16 text as wchar_t: one unit to an element without u, or (where wchar_t is 32 bits) one code point to an
// element with u, with the maps between the two kinds of position.
struct wide_text {
    std::wstring w;
    std::vector<std::size_t> element_of;  // for each unit (and the end), the element holding it
    std::vector<std::size_t> unit_of;     // for each element (and the end), its first unit

    void build(std::u16string_view s, bool unicode) {
        w.clear();
        element_of.assign(s.size() + 1, 0);
        unit_of.clear();
        for (std::size_t i = 0; i < s.size(); i++) {
            element_of[i] = w.size();
            unit_of.push_back(i);
            if constexpr (sizeof(wchar_t) == 4) {
                if (unicode && high(s[i]) && i + 1 < s.size() && low(s[i + 1])) {
                    element_of[i + 1] = w.size();
                    w += static_cast<wchar_t>(0x10000 + ((s[i] - 0xD800) << 10) + (s[i + 1] - 0xDC00));
                    i++;
                    continue;
                }
            }
            w += static_cast<wchar_t>(s[i]);
        }
        element_of[s.size()] = w.size();
        unit_of.push_back(s.size());
    }
};

// A text converted once and reused while the runner asks about the same text.
template <class Text>
struct text_cache {
    const char16_t* data = nullptr;
    std::size_t size = static_cast<std::size_t>(-1);
    Text text;

    const Text& of(std::u16string_view s, bool unicode) {
        if (s.data() != data || s.size() != size) {
            text.build(s, unicode);
            data = s.data();
            size = s.size();
        }
        return text;
    }
};

std::wstring wide_pattern(std::u16string_view p, bool unicode) {
    wide_text t;
    t.build(p, unicode);
    return t.w;
}

// ------------------------------------------------------------------------------------------------------------------
// std::regex

class std_regex final : public exec_regex {
  public:
    std_regex(std::wregex re, bool unicode, bool sticky, std::vector<std::u16string> names)
        : re_(std::move(re)), unicode_(unicode), sticky_(sticky), names_(std::move(names)) {}

    std::size_t groups() const override { return re_.mark_count() + 1; }
    bool unicode() const override { return unicode_; }
    // A view of the name itself: with u"" as the other arm, the conditional's type is a temporary u16string.
    std::u16string_view name(std::size_t g) const override {
        return g < names_.size() ? std::u16string_view(names_[g]) : std::u16string_view();
    }

    result exec(std::u16string_view text, std::size_t from, std::size_t* out, const char** why) override {
        const wide_text& t = cache_.of(text, unicode_);
        std::size_t start = t.element_of[from];
        auto flags = std::regex_constants::match_default;
        if (start > 0) flags |= std::regex_constants::match_prev_avail;
        if (sticky_) flags |= std::regex_constants::match_continuous;
        std::wsmatch m;
        try {
            if (!std::regex_search(t.w.cbegin() + static_cast<std::ptrdiff_t>(start), t.w.cend(), m, re_, flags)) {
                return result::no;
            }
        } catch (const std::regex_error& e) {
            if (e.code() == std::regex_constants::error_complexity || e.code() == std::regex_constants::error_stack) {
                *why = "complexity";
                return result::limit;
            }
            *why = "error";
            return result::error;
        }
        if (out) {
            for (std::size_t g = 0; g < groups(); g++) {
                if (m[g].matched) {
                    out[2 * g] = t.unit_of[static_cast<std::size_t>(m[g].first - t.w.cbegin())];
                    out[2 * g + 1] = t.unit_of[static_cast<std::size_t>(m[g].second - t.w.cbegin())];
                } else {
                    out[2 * g] = out[2 * g + 1] = unset;
                }
            }
        }
        return result::yes;
    }

  private:
    std::wregex re_;
    bool unicode_, sticky_;
    std::vector<std::u16string> names_;
    text_cache<wide_text> cache_;
};

class std_engine final : public exec_engine {
  public:
    // ECMA-262's 3rd edition as the C++ standard modifies it: no u or v flag.
    std::vector<std::string> grammars() const override { return {"legacy"}; }
    std::vector<std::string> calls() const override { return {"std::wregex", "std::regex_search"}; }
    std::unique_ptr<exec_regex> compile(std::u16string_view pattern, std::string_view flags, refusal& why,
                                        std::string& message) override {
        if (has(flags, 's') || has(flags, 'v')) {
            why = refusal::error;
            message = "std::regex has no s or v flag";
            return nullptr;
        }
        bool unicode = has(flags, 'u');
        auto options = std::regex_constants::ECMAScript;
        if (has(flags, 'i')) options |= std::regex_constants::icase;
        if (has(flags, 'm')) options |= std::regex_constants::multiline;
        try {
            return std::make_unique<std_regex>(std::wregex(wide_pattern(pattern, unicode), options), unicode,
                                               has(flags, 'y'), group_names(pattern, false));
        } catch (const std::regex_error& e) {
            why = refusal::parse;
            message = e.what();
            return nullptr;
        }
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Boost.Regex

#ifdef CRE_CPP_HAVE_BOOST
class boost_regex final : public exec_regex {
  public:
    boost_regex(boost::wregex re, bool unicode, bool sticky, bool multiline, std::vector<std::u16string> names)
        : re_(std::move(re)), unicode_(unicode), sticky_(sticky), multiline_(multiline), names_(std::move(names)) {}

    std::size_t groups() const override { return re_.mark_count() + 1; }
    bool unicode() const override { return unicode_; }
    // A view of the name itself: with u"" as the other arm, the conditional's type is a temporary u16string.
    std::u16string_view name(std::size_t g) const override {
        return g < names_.size() ? std::u16string_view(names_[g]) : std::u16string_view();
    }

    result exec(std::u16string_view text, std::size_t from, std::size_t* out, const char** why) override {
        const wide_text& t = cache_.of(text, unicode_);
        std::size_t start = t.element_of[from];
        auto flags = boost::match_default;
        if (!multiline_) flags |= boost::match_single_line;
        if (start > 0) flags |= boost::match_prev_avail;
        if (sticky_) flags |= boost::match_continuous;
        boost::wsmatch m;
        try {
            if (!boost::regex_search(t.w.cbegin() + static_cast<std::ptrdiff_t>(start), t.w.cend(), m, re_, flags)) {
                return result::no;
            }
        } catch (const std::runtime_error&) {
            // Boost throws when a match passes its bound of complexity or of memory.
            *why = "complexity";
            return result::limit;
        }
        if (out) {
            for (std::size_t g = 0; g < groups(); g++) {
                if (m[static_cast<int>(g)].matched) {
                    out[2 * g] = t.unit_of[static_cast<std::size_t>(m[static_cast<int>(g)].first - t.w.cbegin())];
                    out[2 * g + 1] = t.unit_of[static_cast<std::size_t>(m[static_cast<int>(g)].second - t.w.cbegin())];
                } else {
                    out[2 * g] = out[2 * g + 1] = unset;
                }
            }
        }
        return result::yes;
    }

  private:
    boost::wregex re_;
    bool unicode_, sticky_, multiline_;
    std::vector<std::u16string> names_;
    text_cache<wide_text> cache_;
};

class boost_engine final : public exec_engine {
  public:
    std::vector<std::string> calls() const override { return {"boost::wregex", "boost::regex_search"}; }
    std::unique_ptr<exec_regex> compile(std::u16string_view pattern, std::string_view flags, refusal& why,
                                        std::string& message) override {
        if (has(flags, 'v')) {
            why = refusal::error;
            message = "Boost.Regex has no v flag";
            return nullptr;
        }
        bool unicode = has(flags, 'u');
        boost::wregex::flag_type options = boost::wregex::ECMAScript;
        if (has(flags, 'i')) options |= boost::wregex::icase;
        options |= has(flags, 's') ? boost::wregex::mod_s : boost::wregex::no_mod_s;
        try {
            return std::make_unique<boost_regex>(boost::wregex(wide_pattern(pattern, unicode), options), unicode,
                                                 has(flags, 'y'), has(flags, 'm'), group_names(pattern, false));
        } catch (const std::exception& e) {
            why = refusal::parse;
            message = e.what();
            return nullptr;
        }
    }
};
#endif

// ------------------------------------------------------------------------------------------------------------------
// SRELL

#ifdef CRE_CPP_HAVE_SRELL
// Without u or v: each UTF-16 unit an element of a char32_t text, so that SRELL matches by code unit.
struct units32_text {
    std::u32string s;
    void build(std::u16string_view t, bool) {
        s.assign(t.size(), U'\0');
        for (std::size_t i = 0; i < t.size(); i++) s[i] = static_cast<char32_t>(t[i]);
    }
};

template <class Char>
class srell_regex final : public exec_regex {
  public:
    srell_regex(srell::basic_regex<Char> re, bool unicode, bool sticky, std::vector<std::u16string> names)
        : re_(std::move(re)), unicode_(unicode), sticky_(sticky), names_(std::move(names)) {}

    std::size_t groups() const override { return re_.mark_count() + 1; }
    bool unicode() const override { return unicode_; }
    // A view of the name itself: with u"" as the other arm, the conditional's type is a temporary u16string.
    std::u16string_view name(std::size_t g) const override {
        return g < names_.size() ? std::u16string_view(names_[g]) : std::u16string_view();
    }

    result exec(std::u16string_view text, std::size_t from, std::size_t* out, const char** why) override {
        const Char* begin;
        const Char* end;
        if constexpr (std::is_same_v<Char, char16_t>) {
            begin = text.data();
            end = text.data() + text.size();
        } else {
            const units32_text& t = cache_.of(text, false);
            begin = t.s.data();
            end = t.s.data() + t.s.size();
        }
        auto flags = srell::regex_constants::match_default;
        if (sticky_) flags |= srell::regex_constants::match_continuous;
        srell::match_results<const Char*> m;
        try {
            // The three-iterator search: from `from`, with the text before it there for lookbehind and \b.
            if (!srell::regex_search(begin + from, end, begin, m, re_, flags)) return result::no;
        } catch (const srell::regex_error& e) {
            if (e.code() == srell::regex_constants::error_complexity) {
                *why = "complexity";
                return result::limit;
            }
            *why = "error";
            return result::error;
        }
        if (out) {
            for (std::size_t g = 0; g < groups(); g++) {
                if (m[g].matched) {
                    out[2 * g] = static_cast<std::size_t>(m[g].first - begin);
                    out[2 * g + 1] = static_cast<std::size_t>(m[g].second - begin);
                } else {
                    out[2 * g] = out[2 * g + 1] = unset;
                }
            }
        }
        return result::yes;
    }

  private:
    srell::basic_regex<Char> re_;
    bool unicode_, sticky_;
    std::vector<std::u16string> names_;
    text_cache<units32_text> cache_;
};

class srell_engine final : public exec_engine {
  public:
    // SRELL always reads a pattern as the u flag does (and as the v flag does with unicodesets): it has no legacy
    // grammar.
    std::vector<std::string> grammars() const override { return {"unicode", "unicode_sets"}; }
    std::vector<std::string> calls() const override { return {"srell::basic_regex", "srell::regex_search"}; }
    // SRELL's tables are of Unicode 18.0, which the data's overlay for that version answers.
    std::string unicode() const override { return "18.0"; }
    std::unique_ptr<exec_regex> compile(std::u16string_view pattern, std::string_view flags, refusal& why,
                                        std::string& message) override {
        bool unicode = has(flags, 'u') || has(flags, 'v');
        auto options = srell::regex_constants::ECMAScript;
        if (has(flags, 'i')) options |= srell::regex_constants::icase;
        if (has(flags, 'm')) options |= srell::regex_constants::multiline;
        if (has(flags, 's')) options |= srell::regex_constants::dotall;
        if (has(flags, 'v')) options |= srell::regex_constants::unicodesets;
        try {
            if (unicode) {
                return std::make_unique<srell_regex<char16_t>>(
                    srell::basic_regex<char16_t>(pattern.data(), pattern.size(), options), true, has(flags, 'y'),
                    group_names(pattern, has(flags, 'v')));
            }
            std::u32string p32(pattern.begin(), pattern.end());
            return std::make_unique<srell_regex<char32_t>>(srell::basic_regex<char32_t>(p32, options), false,
                                                           has(flags, 'y'), group_names(pattern, false));
        } catch (const srell::regex_error& e) {
            why = e.code() == srell::regex_constants::error_complexity ? refusal::too_large : refusal::parse;
            message = e.what();
            return nullptr;
        }
    }
};
#endif

}  // namespace

std::unique_ptr<exec_engine> make_exec_engine(std::string_view name) {
    if (name == "std") return std::make_unique<std_engine>();
#ifdef CRE_CPP_HAVE_BOOST
    if (name == "boost") return std::make_unique<boost_engine>();
#endif
#ifdef CRE_CPP_HAVE_SRELL
    if (name == "srell") return std::make_unique<srell_engine>();
#endif
    return nullptr;
}

std::vector<std::string> exec_engine_names() {
    std::vector<std::string> names{"std"};
#ifdef CRE_CPP_HAVE_BOOST
    names.push_back("boost");
#endif
#ifdef CRE_CPP_HAVE_SRELL
    names.push_back("srell");
#endif
    return names;
}

}  // namespace dataset

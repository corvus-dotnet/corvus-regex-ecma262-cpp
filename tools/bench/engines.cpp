// The engines of the C++ Industry harness, each counting a workload's matches over its text as a C++ program that
// wants every match would:
//
// - corvus-utf8 and corvus-utf16: this library (corvus/regex_ecma262.hpp) over the text as UTF-8 and as UTF-16, with
//   the workload's flags and u: a global search made once per text, reset for each count, and walked as a range.
// - std: std::regex with std::regex_constants::ECMAScript, over the UTF-8 bytes (it has no UTF-8 mode).
// - boost: Boost.Regex in its ECMAScript (Perl) syntax, over the UTF-8 bytes, with match_single_line unless m and
//   no_mod_s unless s, so that ^, $ and . read as ECMA-262 reads them.
// - srell: SRELL, an ECMAScript library, over the text as UTF-8 (u8cregex), with the workload's flags.
// - re2: RE2, which does not claim ECMA-262, given the pattern as it is with the flags i, m and s.
//
// Each count is the global search of ECMA-262: after an empty match the search goes on one character (one UTF-8
// sequence) later.
#include "engines.h"

#include <corvus/regex_ecma262.hpp>

#include <cstdio>
#include <cstring>
#include <functional>
#include <regex>
#include <string>
#include <string_view>

#define BOOST_REGEX_STANDALONE
#include <boost/regex.hpp>
#include <re2/re2.h>

#include "srell.hpp"

extern "C" {
#include "json.h"
}

namespace {

// RE2's string piece: re2::StringPiece in its older releases, absl::string_view in its newer ones, read from Match.
template <class R, class P>
P* match_piece(bool (R::*)(P, std::size_t, std::size_t, typename R::Anchor, P*, int) const);
using re2_piece = std::remove_pointer_t<decltype(match_piece(&RE2::Match))>;

bool has(const char* flags, char f) { return std::strchr(flags, f) != nullptr; }

// After an empty match at the end `at`, the start of the next UTF-8 character.
std::size_t next_char(const char* text, std::size_t len, std::size_t at) {
    at++;
    while (at < len && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) at++;
    return at;
}

// A count over any engine with a search from a position: search(at, start, end) finds the first match at or after at.
template <class Search>
std::size_t count_with(const char* text, std::size_t len, Search&& search) {
    std::size_t n = 0, at = 0, start = 0, end = 0;
    while (at <= len && search(at, start, end)) {
        n++;
        if (end == start) {
            if (end >= len) break;
            at = next_char(text, len, end);
        } else {
            at = end;
        }
    }
    return n;
}

// The state a count runs over: a function the engine sets up, called through measure's C interface.
struct counter {
    std::function<std::size_t()> count;
    static std::size_t call(void* state) { return static_cast<counter*>(state)->count(); }
};

void set_error(char* error, std::size_t size, const std::string& message) {
    std::snprintf(error, size, "%s", message.c_str());
}

// Times c after one count outside measure, which reports an error (as std::regex's error_complexity) rather than
// letting an exception into the C driver.
bool timed(counter& c, double seconds, int passes, double* median, double* min, double* max, std::size_t* expected,
           std::size_t* reps, char* error, std::size_t error_size) {
    try {
        c.count();
    } catch (const std::exception& e) {
        set_error(error, error_size, e.what());
        return false;
    }
    auto inner = std::move(c.count);
    c.count = [inner]() -> std::size_t {
        try {
            return inner();
        } catch (...) {
            return static_cast<std::size_t>(-1);
        }
    };
    measure(seconds, passes, counter::call, &c, median, min, max, expected, reps);
    return true;
}

}  // namespace

extern "C" bool engine_run(const char* engine, const char* pattern, const char* flags, const char* flags_u,
                           const char* text, std::size_t len, double seconds, int passes, double* median, double* min,
                           double* max, std::size_t* expected, std::size_t* reps, char* error, std::size_t error_size) {
    std::string_view name = engine;
    counter c;
    try {
        if (name == "corvus-utf8") {
            corvus::ecma262::regex re(pattern, flags_u);
            auto matches = re.find_all(std::string_view(text, len));
            c.count = [&matches] {
                matches.reset();
                std::size_t n = 0;
                for (corvus::ecma262::match m : matches) {
                    (void)m;
                    n++;
                }
                return n;
            };
            return timed(c, seconds, passes, median, min, max, expected, reps, error, error_size);
        }
        if (name == "corvus-utf16") {
            std::size_t len16;
            uint16_t* units = utf8_to_utf16(text, len, &len16);
            std::u16string text16(reinterpret_cast<const char16_t*>(units), len16);
            free(units);
            corvus::ecma262::regex re(pattern, flags_u);
            auto matches = re.find_all(text16);
            c.count = [&matches] {
                matches.reset();
                std::size_t n = 0;
                for (corvus::ecma262::match m : matches) {
                    (void)m;
                    n++;
                }
                return n;
            };
            return timed(c, seconds, passes, median, min, max, expected, reps, error, error_size);
        }
        if (name == "std") {
            auto options = std::regex_constants::ECMAScript;
            if (has(flags, 'i')) options |= std::regex_constants::icase;
            if (has(flags, 'm')) options |= std::regex_constants::multiline;
            if (has(flags, 's')) {
                set_error(error, error_size, "std::regex has no s flag");
                return false;
            }
            std::regex re(pattern, options);
            c.count = [&] {
                return count_with(text, len, [&](std::size_t at, std::size_t& s, std::size_t& e) {
                    std::cmatch m;
                    auto f = at > 0 ? std::regex_constants::match_prev_avail : std::regex_constants::match_default;
                    if (!std::regex_search(text + at, text + len, m, re, f)) return false;
                    s = static_cast<std::size_t>(m[0].first - text);
                    e = static_cast<std::size_t>(m[0].second - text);
                    return true;
                });
            };
            return timed(c, seconds, passes, median, min, max, expected, reps, error, error_size);
        }
        if (name == "boost") {
            boost::regex::flag_type options = boost::regex::ECMAScript;
            if (has(flags, 'i')) options |= boost::regex::icase;
            options |= has(flags, 's') ? boost::regex::mod_s : boost::regex::no_mod_s;
            boost::regex re(pattern, options);
            bool multiline = has(flags, 'm');
            c.count = [&] {
                return count_with(text, len, [&](std::size_t at, std::size_t& s, std::size_t& e) {
                    boost::cmatch m;
                    auto f = boost::match_default;
                    if (!multiline) f |= boost::match_single_line;
                    if (at > 0) f |= boost::match_prev_avail;
                    if (!boost::regex_search(text + at, text + len, m, re, f)) return false;
                    s = static_cast<std::size_t>(m[0].first - text);
                    e = static_cast<std::size_t>(m[0].second - text);
                    return true;
                });
            };
            return timed(c, seconds, passes, median, min, max, expected, reps, error, error_size);
        }
        if (name == "srell") {
            auto options = srell::regex_constants::ECMAScript;
            if (has(flags, 'i')) options |= srell::regex_constants::icase;
            if (has(flags, 'm')) options |= srell::regex_constants::multiline;
            if (has(flags, 's')) options |= srell::regex_constants::dotall;
            if (has(flags, 'v')) options |= srell::regex_constants::unicodesets;
            srell::u8cregex re(pattern, options);
            c.count = [&] {
                return count_with(text, len, [&](std::size_t at, std::size_t& s, std::size_t& e) {
                    srell::cmatch m;
                    if (!srell::regex_search(text + at, text + len, text, m, re)) return false;
                    s = static_cast<std::size_t>(m[0].first - text);
                    e = static_cast<std::size_t>(m[0].second - text);
                    return true;
                });
            };
            return timed(c, seconds, passes, median, min, max, expected, reps, error, error_size);
        }
        if (name == "re2") {
            RE2::Options options;
            options.set_log_errors(false);
            options.set_case_sensitive(!has(flags, 'i'));
            options.set_dot_nl(has(flags, 's'));
            std::string p = has(flags, 'm') ? std::string("(?m)") + pattern : std::string(pattern);
            RE2 re(p, options);
            if (!re.ok()) {
                set_error(error, error_size, re.error());
                return false;
            }
            c.count = [&] {
                return count_with(text, len, [&](std::size_t at, std::size_t& s, std::size_t& e) {
                    re2_piece m;
                    if (!re.Match(re2_piece(text, len), at, len, RE2::UNANCHORED, &m, 1)) return false;
                    s = static_cast<std::size_t>(m.data() - text);
                    e = s + m.size();
                    return true;
                });
            };
            return timed(c, seconds, passes, median, min, max, expected, reps, error, error_size);
        }
    } catch (const std::exception& e) {
        set_error(error, error_size, e.what());
        return false;
    }
    return false;
}

extern "C" bool engine_claims_ecma262(const char* engine) {
    std::string_view name = engine;
    return name != "re2";
}

extern "C" const char* engine_label(const char* engine) {
    std::string_view name = engine;
    if (name == "corvus-utf8") return "corvus-regex-ecma262 UTF-8";
    if (name == "corvus-utf16") return "corvus-regex-ecma262 UTF-16";
    if (name == "std") return "std::regex";
    if (name == "boost") return "Boost.Regex";
    if (name == "srell") return "SRELL";
    if (name == "re2") return "RE2";
    return engine;
}

extern "C" const char* engine_version(const char* engine) {
    std::string_view name = engine;
    static std::string stdlib =
#if defined(_LIBCPP_VERSION)
        "libc++ " + std::to_string(_LIBCPP_VERSION);
#elif defined(_GLIBCXX_RELEASE)
        "libstdc++ " + std::to_string(_GLIBCXX_RELEASE);
#else
        std::string();
#endif
    // Boost.Regex in its standalone mode has no BOOST_VERSION: the build names the release it fetched.
    static std::string boost_version = CRE_CPP_BOOST_VERSION;
    static std::string srell_version = std::to_string(SRELL_HPP_ / 100) + "." + std::to_string(SRELL_HPP_ % 100);
    if (name == "std") return stdlib.c_str();
    if (name == "boost") return boost_version.c_str();
    if (name == "srell") return srell_version.c_str();
#ifdef CRE_CPP_RE2_VERSION
    if (name == "re2") return CRE_CPP_RE2_VERSION;
#endif
    return "";
}

extern "C" const char* engine_compiler(void) {
#if defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC";
#else
    return "";
#endif
}

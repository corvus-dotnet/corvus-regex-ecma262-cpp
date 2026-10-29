# Getting started with C++

## Introduction

`corvus-regex-ecma262` for C++ is one C++20 header, `corvus/regex_ecma262.hpp`, whose patterns answer as ECMAScript
does. `std::regex`'s ECMAScript grammar is ECMAScript 3's, read over bytes; this library has today's ECMA-262, with
its meaning for `\d`, `\w`, `\s`, `.`, case folding and Unicode properties, so a pattern written for JavaScript, or
taken from a JSON Schema, matches the same texts here as it does there. The header is over the corvus-regex-ecma262 C
library, built from the Rust crate and held to V8's answers on the ECMA-262 test data.

This guide covers:

- **Setup**: adding the library with CMake, vcpkg or Conan.
- **Patterns and flags**: the flags are ECMA-262's letters, and what each one does.
- **The edition and the version of Unicode**: the grammar of ECMAScript 2020, and the Unicode tables a pattern reads.
- **Matching**: `is_match`, `find`, `captures`, the global search as a C++20 range, `replace` and `split`, and the
  forms that allocate nothing.
- **Timeouts**: the limit of time and of memory a match may take.
- **UTF-8 and UTF-16**: the type of the text chooses the encoding, with no transcoding.
- **Differences from std::regex**: what to expect when the two meet.

## Setup

The header needs a C++20 compiler (GCC 11, Clang 16, Apple Clang 15 or MSVC 19.30 and later) and CMake 3.24. With
FetchContent, the build downloads the C library's release archive for the platform (Linux with glibc or musl, macOS
and Windows, each on x64 and arm64) when it finds none installed:

```cmake
include(FetchContent)
FetchContent_Declare(corvus_regex_ecma262_cpp
  GIT_REPOSITORY https://github.com/corvus-dotnet/corvus-regex-ecma262-cpp
  GIT_TAG v0.2.5)
FetchContent_MakeAvailable(corvus_regex_ecma262_cpp)

target_link_libraries(app PRIVATE corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp)
```

`corvus_regex_ecma262_cpp_static` links the C library's static library in place of its shared one. The repository
also has a vcpkg port and a Conan recipe with the same targets (see the [README](../README.md)).

```cpp
#include <corvus/regex_ecma262.hpp>
```

## Patterns and flags

A pattern is written as it would be in JavaScript, and the flags are the letters `RegExp` takes, as a string:

| Flag | Meaning |
|---|---|
| `d` | Accepted for completeness; indices are always kept. |
| `g` | `replace` replaces every match; without it, the first, as `String.prototype.replace` does. |
| `i` | Canonicalize of the mode in force: simple case folding with `u` or `v`, `toUpperCase` without. Never the locale. |
| `m` | `^` and `$` at line terminators. |
| `s` | The dot matches line terminators. |
| `u` | The `u` flag grammar; matched by code point. |
| `v` | Set notation and properties of strings; matched by code point. `u` and `v` together is an error, as in ECMA-262. |
| `y` | A match only at the start position. |

```cpp
#include <corvus/regex_ecma262.hpp>

#include <cassert>
#include <string_view>
#include <vector>

namespace ecma = corvus::ecma262;

int main() {
    ecma::regex a(R"(\p{Script=Greek}+)", "u");
    ecma::regex b(R"([[a-z]--[aeiou]]+)", "v");
    ecma::regex c(R"(^(?<year>\d{4})-(?<month>\d{2})$)", "u");

    assert(a.is_match(u8"αβγ"));
    assert(b.is_match("rhythm"));
    assert((c.group_names() == std::vector<std::string_view>{"year", "month"}));
}
```

With no flag the pattern is read with the Annex B grammar and matched by UTF-16 code unit, which is what ECMA-262 does
with no flag. JSON Schema's reading, the `u` flag first and Annex B when that refuses, is two calls of
`regex::try_compile`, which costs no exception and takes the match timeout in its options, so no default timeout is
needed:

```cpp
#include <corvus/regex_ecma262.hpp>

#include <cassert>
#include <chrono>
#include <optional>
#include <string_view>
#include <system_error>

namespace ecma = corvus::ecma262;
using namespace std::chrono_literals;

std::optional<ecma::regex> schema_pattern(std::string_view pattern, std::error_code& ec) {
    ecma::regex::options opts{.match_timeout = 1s};
    auto re = ecma::regex::try_compile(pattern, "u", opts, ec);
    if (!re) re = ecma::regex::try_compile(pattern, "", opts, ec);
    return re;
}

int main() {
    std::error_code ec;
    assert(schema_pattern(R"(^\p{L}+$)", ec)->flags() == "u");
    assert(schema_pattern(R"(\-)", ec)->flags() == "");  // an identity escape, which only Annex B allows
}
```

A pattern that is not valid throws `regex_error`, whose `reason()` is `errc::invalid_pattern` and whose `what()` says
why; invalid flags are `errc::invalid_flags`. Every pattern has deterministic limits of size, and
`errc::pattern_too_large` says when one is passed. `try_compile` sets the same `errc` in its `std::error_code`.

```cpp
#include <corvus/regex_ecma262.hpp>

#include <cassert>

namespace ecma = corvus::ecma262;

int main() {
    try {
        ecma::regex bad("a{2,1}", "u");
        assert(false);
    } catch (const ecma::regex_error& e) {
        assert(e.reason() == ecma::errc::invalid_pattern);
    }
    assert(!ecma::regex::is_valid("a", "uv"));
}
```

## The edition and the version of Unicode

By default a pattern is read with today's grammar, ECMAScript 2025 (`edition::current`), and with the tables of the
version of Unicode that V8 carries, 17 today (`unicode_version::v8`). Either can be overridden when the pattern is
compiled, each independently of the other.

JSON Schema 2020-12 names the grammar of ECMAScript 2020, which `edition::es2020` selects in place of the default. It
refuses what later editions added, and nothing else: the `d` and `v` flags, modifier groups such as `(?i:...)`, a group
name used by two groups, and a property name or value that is not in the 2020 tables (such as `\p{Script=Yezidi}`). A
pattern it accepts has the same answers as in today's grammar.

The version of Unicode is chosen apart from the edition: `unicode_version::v13`, `v17`, `v18`, `latest` (18 today) or
`v8`, the version of the V8 the answers are held to (17 today) and the default. It decides the members of each
`\p{...}`, the characters of a group name and the case folding of the `i` flag. `regex::unicode_version()` reports the
version a pattern was compiled with. Both are set in a `regex::options` and read when the pattern is compiled, so a
match costs the same whichever is chosen.

```cpp
#include <corvus/regex_ecma262.hpp>

#include <cassert>
#include <chrono>

namespace ecma = corvus::ecma262;
using namespace std::chrono_literals;

int main() {
    // ECMAScript 2020 as its edition was published, with the Unicode of its year.
    ecma::regex::options opts{
        .match_timeout = 1s, .edition = ecma::edition::es2020, .unicode_version = ecma::unicode_version::v13};
    ecma::regex greek(R"(^\p{Script=Greek}+$)", "u", opts);
    assert(greek.is_match(u8"αβγ") && greek.unicode_version() == 13);

    // Vithkuqi is in Unicode 14 and later, so not in Unicode 13.
    assert(!ecma::regex::is_valid(R"(\p{Script=Vithkuqi})", "u", ecma::edition::current, ecma::unicode_version::v13));
}
```

## Matching

The members are those of a `RegExp`, with the position as an argument where JavaScript keeps `lastIndex`. A `match` is
where a match or a group is in its text, `start` to `end`; `in(text)` is its text:

```cpp
#include <corvus/regex_ecma262.hpp>

#include <cassert>
#include <optional>
#include <string_view>
#include <vector>

namespace ecma = corvus::ecma262;

int main() {
    ecma::regex date(R"((?<year>\d{4})-(?<month>\d{2}))", "gu");
    std::string_view text = "due 2026-10, then 2027-01";

    assert(date.is_match(text));
    std::optional<ecma::match> m = date.find(text);
    assert(m && m->start == 4 && m->end == 11 && m->in(text) == "2026-10");

    auto groups = date.captures(text);
    assert(groups && groups->str("year") == "2026" && groups->str(2) == "10");

    // Every match of a global search, as matchAll makes it, as a C++20 range. The text must outlive the range.
    std::vector<std::string_view> found;
    for (ecma::match each : date.find_all(text)) found.push_back(each.in(text));
    assert((found == std::vector<std::string_view>{"2026-10", "2027-01"}));

    // replace replaces every match with the g flag, and the first without it, with every $ form of ECMA-262.
    assert(date.replace(text, "$<month>/$1") == "due 10/2026, then 01/2027");

    // split puts what the groups captured between the pieces; a group that did not take part is nullopt.
    auto pieces = ecma::regex(R"(\s*(;)\s*)").split("a ; b;c");
    assert(pieces.size() == 5 && pieces[1] == ";" && pieces[4] == "c");
}
```

`find(text, start)` is `exec` with `lastIndex` set to `start`, and `y` anchors it there. `captures`, `replace` and
`split` allocate what they return. `is_match`, `find`, the `find_all` range and `captures_read`, which writes a match's
groups into a span the caller gives, allocate nothing once the thread has read a match with as many groups:

```cpp
#include <corvus/regex_ecma262.hpp>

#include <array>
#include <cassert>

namespace ecma = corvus::ecma262;

int main() {
    ecma::regex date(R"((?<year>\d{4})-(?<month>\d{2}))", "u");
    std::array<ecma::match, 3> groups;
    assert(date.captures_read("due 2026-10", 0, groups));
    assert((groups[1] == ecma::match{4, 8}));
}
```

A `regex` is immutable, so any number of threads may use one at once, and copying one shares the compiled pattern.

## Timeouts

A match has no limit by default. A `regex` made with a match timeout abandons a match that reaches it, or that reaches
the cap on backtracking memory the timeout brings, with `regex_error` and `errc::match_limit`, whatever the pattern:

```cpp
#include <corvus/regex_ecma262.hpp>

#include <cassert>
#include <chrono>
#include <string>

namespace ecma = corvus::ecma262;
using namespace std::chrono_literals;

int main() {
    ecma::regex slow(R"(^(?:(a+)\1*)+$)", "", 100ms);
    assert(slow.match_timeout() == 100ms);
    try {
        slow.is_match(std::string(60, 'a') + "b");
        assert(false);
    } catch (const ecma::regex_error& e) {
        assert(e.reason() == ecma::errc::match_limit);
    }
}
```

A `regex` made without a timeout takes `regex::default_match_timeout()`, which is none unless
`regex::set_default_match_timeout` sets it. A program that matches patterns or texts from outside should set one, for
example with `regex::set_default_match_timeout(1s)` when it starts. The overloads of `is_match`, `find` and `captures`
that take a `std::chrono::milliseconds` give one match a limit of its own.

A pattern without a backreference is matched in time linear in the text, and no pattern can overflow the stack, so the
timeout is what bounds the others.

## UTF-8 and UTF-16

The type of the text chooses the encoding, and nothing is transcoded. `char` and `char8_t` text (`std::string_view`,
`std::u8string_view` and their strings) is UTF-8, with positions in bytes. `char16_t` text (and `wchar_t` on Windows)
is UTF-16, lone surrogates and all, with positions in code units. UTF-8 is checked before it is read: text that is not
UTF-8 throws `errc::invalid_utf8`, whose `offset()` is the first byte that is not, and is never matched or read as
U+FFFD.

```cpp
#include <corvus/regex_ecma262.hpp>

#include <cassert>
#include <string_view>

namespace ecma = corvus::ecma262;

int main() {
    ecma::regex dot_b(".b", "u");

    assert((dot_b.find(u8"a😀b") == ecma::match{1, 6}));  // bytes of UTF-8
    assert((dot_b.find(u"a😀b") == ecma::match{1, 4}));   // UTF-16 code units

    try {
        dot_b.is_match(std::string_view("\xff", 1));
        assert(false);
    } catch (const ecma::regex_error& e) {
        assert(e.reason() == ecma::errc::invalid_utf8 && e.offset() == 0);
    }
}
```

Under the `u` and `v` flags a character is a code point in both encodings, so a class, a dot or a quantifier takes a
four-byte sequence as one character. With no flag ECMA-262 matches by UTF-16 code unit; over UTF-8 the library keeps
that meaning, so a match can start or end between the halves of a character beyond the Basic Multilingual Plane, at
the character's third byte.

## Differences from std::regex

The members follow `RegExp`; the meaning follows ECMA-262. The differences a user coming from `std::regex` will meet:

- **The grammar.** `std::regex`'s ECMAScript grammar is ECMAScript 3's. This is today's: lookbehind, named groups,
  modifier groups, Unicode property escapes, and the `u`, `v`, `s`, `y` and `d` flags. Boost.Regex's default grammar
  is Perl's.
- **The text.** `std::regex` reads a `std::string` as bytes, so `.` matches a byte of a character. This reads UTF-8
  and UTF-16 by character, refuses text that is not UTF-8, and gives positions in the text's own units.
- **The character classes.** `\d` is `[0-9]`, `\w` is the 63 ASCII word characters, `\s` is ECMAScript's white space
  and line terminators, and `.` excludes the four line terminators (LF, CR, U+2028, U+2029), whatever the locale.
- **Replacement.** A template writes a group as `$1` or `$<name>`, and `$&`, `` $` `` and `$'` are the match and the
  text before and after it; `replace` replaces every match only with the `g` flag, and `replace_all` always does.
- **Split.** `split` is JavaScript's, which puts what the groups captured between the pieces; the standard library has
  none.
- **Timeouts.** `std::regex` matches by recursion and can overflow the stack on a long text. Here no pattern can, a
  pattern without a backreference is matched in time linear in the text, and a timeout bounds the others.

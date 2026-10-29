# Getting started with C++

`corvus-regex-ecma262` for C++ is one C++20 header, `corvus/regex_ecma262.hpp`, over the corvus-regex-ecma262 C
library. Its patterns have the grammar and the answers of JavaScript's `RegExp`, held to V8's answers on the ECMA-262
test data, over UTF-8 (`std::string_view`, `std::u8string_view`) and UTF-16 (`std::u16string_view`) with no
transcoding. A compiled `regex` is immutable and shared freely between threads.

## Install

The header needs a C++20 compiler (GCC 11, Clang 16, Apple Clang 15 or MSVC 19.30 and later) and CMake 3.24. With
FetchContent, the build downloads the C library's release archive for the platform when it finds none installed:

```cmake
include(FetchContent)
FetchContent_Declare(corvus_regex_ecma262_cpp
  GIT_REPOSITORY https://github.com/corvus-dotnet/corvus-regex-ecma262-cpp
  GIT_TAG v0.2.3)
FetchContent_MakeAvailable(corvus_regex_ecma262_cpp)

target_link_libraries(app PRIVATE corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp)
```

`corvus_regex_ecma262_cpp_static` links the C library's static library in place of its shared one. The repository
also has a vcpkg port and a Conan recipe with the same targets (see the [README](../README.md)).

## Compile a pattern

A `regex` takes a pattern and its flags as `new RegExp(pattern, flags)` takes them: `d`, `g`, `i`, `m`, `s`, `u`, `v`
and `y`. Without `u` or `v` a pattern is read by the Annex B grammar and matched by UTF-16 code unit, as ECMA-262
reads it; with them, by code point. A pattern that is not valid throws `regex_error`, whose `reason()` is an `errc`;
`regex::try_compile` sets a `std::error_code` in place of throwing.

```cpp
#include <corvus/regex_ecma262.hpp>

#include <cassert>
#include <chrono>
#include <system_error>

namespace ecma = corvus::ecma262;
using namespace std::chrono_literals;

int main() {
    ecma::regex date(R"((?<year>\d{4})-(?<month>\d{2}))", "gu");
    assert(date.group_count() == 2);
    assert(date.flags() == "gu");

    try {
        ecma::regex bad("a{2,1}", "u");
        assert(false);
    } catch (const ecma::regex_error& e) {
        assert(e.reason() == ecma::errc::invalid_pattern);
    }

    std::error_code ec;
    auto none = ecma::regex::try_compile("a", "uv", {.match_timeout = 1s}, ec);
    assert(!none && ec == ecma::errc::invalid_flags);

    assert(!ecma::regex::is_valid("(?<n>a)(?<n>b)", "u"));
}
```

## Match

A `match` is where a match or a group is in its text, `start` to `end`, in the text's units; `in(text)` is its text.
`captures` gives every group, by number and by name.

```cpp
#include <corvus/regex_ecma262.hpp>

#include <cassert>
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

## UTF-8 and UTF-16

The type of the text chooses the encoding. `char` and `char8_t` text is UTF-8, and is checked before it is read: text
that is not UTF-8 throws `errc::invalid_utf8`, and is never matched or read as U+FFFD. `char16_t` text (and `wchar_t`
on Windows) is UTF-16, lone surrogates and all, with positions in code units.

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

## Patterns and texts from outside

A match has no limit by default. A program that matches a pattern or a text it did not write gives the pattern a match
timeout, as a .NET `Regex` has: a match that runs past it, or whose backtracking memory reaches its cap, throws
`errc::match_limit`. No pattern can overflow the stack. `regex::set_default_match_timeout` sets the timeout of every
regex made after it, and the overloads that take a `std::chrono::milliseconds` give one match a limit of its own.

```cpp
#include <corvus/regex_ecma262.hpp>

#include <cassert>
#include <chrono>
#include <string>

namespace ecma = corvus::ecma262;
using namespace std::chrono_literals;

int main() {
    ecma::regex slow(R"(^(?:(a+)\1*)+$)", "", 100ms);
    try {
        slow.is_match(std::string(60, 'a') + "b");
        assert(false);
    } catch (const ecma::regex_error& e) {
        assert(e.reason() == ecma::errc::match_limit);
    }
}
```

## The edition and the version of Unicode

JSON Schema 2020-12 names the grammar of ECMAScript 2020. `edition::es2020` refuses what later editions added: the
`d` and `v` flags, modifier groups, a group name used twice, and property names outside the 2020 tables. The version
of Unicode is chosen apart from it: `unicode_version::v13`, `v17`, `v18`, `latest` (18) or `v8` (V8's, 17, the
default). Both are set in a `regex::options` and read when the pattern is compiled, so a match costs the same whatever
they are.

```cpp
#include <corvus/regex_ecma262.hpp>

#include <cassert>
#include <chrono>

namespace ecma = corvus::ecma262;
using namespace std::chrono_literals;

int main() {
    ecma::regex::options opts{
        .match_timeout = 1s, .edition = ecma::edition::es2020, .unicode_version = ecma::unicode_version::v13};
    ecma::regex greek(R"(^\p{Script=Greek}+$)", "u", opts);
    assert(greek.is_match(u8"αβγ"));
    assert(greek.unicode_version() == 13);

    // Vithkuqi is in Unicode 14 and later, so not in Unicode 13.
    assert(!ecma::regex::is_valid(R"(\p{Script=Vithkuqi})", "u", ecma::edition::current, ecma::unicode_version::v13));
}
```

## Differences from std::regex

- `std::regex`'s ECMAScript grammar is ECMAScript 3's. This is today's: lookbehind, named groups, modifier groups,
  Unicode property escapes, and the `u`, `v`, `s`, `y` and `d` flags. Boost.Regex's default grammar is Perl's.
- `std::regex` reads a `std::string` as bytes, so `.` matches a byte of a character. This reads UTF-8 and UTF-16 by
  character, refuses text that is not UTF-8, and gives positions in the text's own units.
- `\d` is `[0-9]`, `\w` the 63 ASCII word characters, `\s` ECMAScript's white space and line terminators, and `.`
  excludes LF, CR, U+2028 and U+2029, whatever the locale.
- A replacement template writes a group as `$1` or `$<name>`, and `$&`, `` $` `` and `$'` are the match and the text
  before and after it. `replace` replaces every match only with the `g` flag; `replace_all` always does.
- `split` is JavaScript's, which puts what the groups captured between the pieces; the standard library has none.
- `std::regex` matches by recursion and can overflow the stack on a long text. Here no pattern can, a pattern without
  a backreference is matched in time linear in the text, and a pattern from outside should be given a match timeout,
  which bounds every match.

## Where next

- [The API reference](api.md), generated from the comments of the header.
- [The README](../README.md).
- [The benchmark page](https://corvus-oss.org/corvus-regex-ecma262-benchmarks/), with every engine measured nightly.

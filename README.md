# corvus-regex-ecma262 for C++

Regular expressions with the grammar and the answers of ECMA-262, the `RegExp` of JavaScript, for C++20: one header
over the [corvus-regex-ecma262](https://github.com/corvus-dotnet/corvus-regex-ecma262-rs) C library.

- Every flag: `d`, `g`, `i`, `m`, `s`, `u`, `v` and `y`, with the grammar of each. Without `u` or `v` a pattern is
  read by the Annex B grammar and matched by UTF-16 code unit, as ECMA-262 matches it; with them, by code point. The
  `v` flag has nested classes, the difference and the intersection of classes, strings in a class and the properties
  of strings.
- ECMAScript 2025: lookbehind, named groups (and a name shared between alternatives), modifier groups such as
  `(?i:...)`, and every Unicode property ECMA-262 lists, from Unicode 17 tables the library carries, so a pattern
  answers the same on every platform.
- Text as `std::string_view` or `std::u8string_view` (UTF-8) or as `std::u16string_view` (UTF-16, lone surrogates and
  all; `std::wstring_view` on Windows). Nothing is transcoded, and positions are in the text's own units.
- `is_match`, `find`, `captures`, a global search as a C++20 range (`find_all`, `captures_all`), `replace` with every
  `$` form of `GetSubstitution`, and `split`, each as `RegExp` and `String.prototype` define it.
- A pattern without a backreference is matched in time proportional to its size times the text, and a match can be
  given a limit of time, so neither a pattern nor a text from outside can make a program hang.

**Compliance.** Through this header, over the
[ECMA-262 test data](https://github.com/corvus-dotnet/ecma262-regex-test-data) (what V8 says about 117,889 patterns
and 5,884,566 texts, test262's and `exec`, the global search, `replace` and `split` among them), the library gives
26,950,829 right answers and none wrong, refuses no valid pattern, accepts no invalid one and reaches no limit, over
UTF-8 and UTF-16.
CI runs the whole data on Linux and Windows on every change.

## Install

The header needs a C++20 compiler (GCC 11, Clang 16, Apple Clang 15 or MSVC 19.30 and later) and CMake 3.24. The C
library comes from its release archives, one for each platform (Linux with glibc or musl, macOS and Windows, each on
x64 and arm64), which the CMake build downloads and checks when it finds no C library installed.

With FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(corvus_regex_ecma262_cpp
  GIT_REPOSITORY https://github.com/corvus-dotnet/corvus-regex-ecma262-cpp
  GIT_TAG v0.1.2)
FetchContent_MakeAvailable(corvus_regex_ecma262_cpp)

target_link_libraries(app PRIVATE corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp)
```

`corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp` links the C library's shared library (on Windows, copy its DLL
next to your program, for example with `$<TARGET_RUNTIME_DLLS:app>`), and
`corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp_static` links its static library.

Installed, with `cmake --install`, the package is found as any other:

```cmake
find_package(corvus_regex_ecma262_cpp 0.1 CONFIG REQUIRED)
```

To use a C library you built or unpacked yourself, put its directory on `CMAKE_PREFIX_PATH`; with
`-DCORVUS_REGEX_ECMA262_FETCH=OFF` the build never downloads.

The repository also has a vcpkg port (`ports/corvus-regex-ecma262-cpp`, for `--overlay-ports`) and a Conan recipe
(`conanfile.py`: `conan create .`), each with the same CMake targets, which CI builds a consumer with on Linux, macOS
and Windows.

## Getting started

```cpp
#include <corvus/regex_ecma262.hpp>

namespace re = corvus::ecma262;

// The flags are those of new RegExp(pattern, flags).
re::regex date(R"((?<year>\d{4})-(?<month>\d{2}))", "gu");

date.is_match("due 2026-10");                       // true

if (auto c = date.captures("due 2026-10")) {
    std::string_view year = *c->str("year");        // "2026"
}

// Every match of a global search, as a range.
std::string text = "2026-10, 2027-01";
for (re::match m : date.find_all(text)) {
    std::string_view found = m.in(std::string_view(text));  // "2026-10", then "2027-01"
}

// replace replaces every match with the g flag, and the first without it.
date.replace(text, "$<month>/$1");                  // "10/2026, 01/2027"

// split splices in what the groups captured; a group that did not take part is an empty optional.
re::regex(R"(\s*(;)\s*)").split("a ; b;c");         // a, ;, b, ;, c

// UTF-16 text is matched as it is, positions in code units.
date.find(u"café 2026-12")->start;             // 5
```

The whole example is [examples/basic.cpp](examples/basic.cpp), which CI builds and runs.

## The API

| Member | What it does |
|---|---|
| `regex(pattern, flags)` | Compiles a pattern as `new RegExp(pattern, flags)` does; throws `regex_error` when it is not valid. |
| `regex::is_valid(pattern, flags)` | Whether a pattern is valid, without compiling it. |
| `is_match(text, start = 0)` | `RegExp.prototype.test` with `lastIndex` at `start` (at `start` alone with `y`). |
| `find(text, start = 0)` | The first match at or after `start`, as `exec` finds it: an `optional<match>`. |
| `captures(text, start = 0)` | The first match with its groups, by number and by name: an `optional<captures>`. |
| `captures_read(text, start, span<match>)` | The groups into a buffer you own, allocating nothing once warm. |
| `find_all(text)` | A global search as `matchAll` makes it: an input range of `match`. |
| `captures_all(text)` | A global search with each match's groups: an input range of `captures`. |
| `replace(text, replacement)` | `String.prototype.replace`: the first match, or every match with `g`. |
| `replace_all(text, replacement)` | `String.prototype.replaceAll`: every match. |
| `split(text, limit = no_limit)` | `String.prototype.split`: a vector of `optional<string>`. |
| `source()`, `flags()`, `group_count()`, `group_names()` | What the pattern is. |

A `match` is a `start` and an `end` in the text's units; a group that did not take part is `npos` to `npos` and is
`false`, and `m.in(text)` is its part of the text. The `find`, `is_match` and `captures` overloads that take a
`std::chrono::milliseconds` stop a match that reaches the limit with `errc::match_limit`.

**Text and positions.** UTF-8 text is checked: text that is not UTF-8 throws `errc::invalid_utf8`, and
`regex_error::offset()` is the offset of the first bad byte. UTF-16 text is not checked, and may hold lone surrogates.
Without `u` or `v` a pattern is matched by UTF-16 code unit, so over UTF-8 a match can start or end between the halves
of a character beyond the Basic Multilingual Plane; such a position is the offset of the character's third byte, and
`replace` and `split` write such a half character as U+FFFD.

**Errors.** Every failure throws `regex_error`, a `std::system_error` whose `code()` is in `corvus::ecma262::category()`
and whose `reason()` is an `errc`: `invalid_pattern`, `invalid_flags`, `pattern_too_large`, `invalid_utf8`,
`invalid_argument` (a start beyond the text or inside a character), `match_limit` or `panic` (a bug in the library,
caught; the library remains usable).

**Threads and lifetimes.** A `regex` is immutable: any number of threads may use one at once, and a copy shares the
compiled pattern. A global search keeps its `regex` alive but not its text, as a `std::string_view` does not, so the
text must outlive the range. `native_handle()` gives the C library's handle, for anything the header does not wrap.

## Benchmarks

The Industry workloads of dotnet/performance, this library against `std::regex` (its ECMAScript grammar),
Boost.Regex, SRELL (an ECMAScript library), RE2 and V8, each engine with its compliance with the test data beside its
figures, are on the central page,
[corvus-oss.org/corvus-regex-ecma262-benchmarks](https://corvus-oss.org/corvus-regex-ecma262-benchmarks/), measured
nightly on one runner with every other Corvus ECMA-262 engine.

## Repository

- `include/corvus/regex_ecma262.hpp`: the library.
- `cmake/`: the package, and the download of the C library's archive (`FetchCLibrary.cmake`, with each archive's hash).
- `tests/`: the tests, and a consumer project built against the installed package; `examples/`: the README's example.
- `tools/dataset`: the data-set runner, which judges this library (or, built with `-DCRE_CPP_COMPETITORS=ON`,
  std::regex, Boost.Regex or SRELL) by the test data's own rule: `cmake -S . -B build -DCRE_CPP_TOOLS=ON`, then
  `build/tools/dataset <ecma262-regex-test-data>/data wrong.tsv [--engine std|boost|srell --isolate on]`.
- `tools/bench`: the benchmark harness and the script the image runs (`tools/bench/run.sh`), under the interleaved
  protocol every Corvus ECMA-262 engine's harness follows; `Dockerfile`: the image the central page runs.

## Licence

Apache-2.0. See [LICENSE](LICENSE).

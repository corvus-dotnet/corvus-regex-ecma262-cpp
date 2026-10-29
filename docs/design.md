# corvus-regex-ecma262 for C++: design

An outline of the library as it is: what it offers, how it is built and why, and how it is tested and measured. The
[getting-started page](getting-started.md) shows how to use it.

## 1. What it is

A header-only C++20 library, `corvus/regex_ecma262.hpp`, of regular expressions with ECMA-262's grammar and answers
(ECMAScript 2025), over UTF-8 and UTF-16. It is a binding to the corvus-regex-ecma262 C library, built from the Rust
crate, whose engine it uses as it is.

**Why ECMA-262.** JSON Schema, OpenAPI and many wire formats specify their patterns as ECMAScript regular
expressions. `std::regex`'s ECMAScript grammar is ECMAScript 3's, read over bytes, and Boost.Regex's default is Perl's,
so a translation to them can never be exact. An engine that reads ECMA-262 directly, held to V8's answers over a large
data set (section 6), needs no translation.

## 2. Public surface

**Names.** Namespace `corvus::ecma262`: a `regex`, with `is_match`, `find`, `captures`, `captures_read`, `find_all`
and `captures_all` (C++20 ranges), `replace`, `replace_all` and `split`, each taking the position where JavaScript keeps
`lastIndex`. A `match` is a `start` and an `end`; `in(text)` is its text. The names follow `RegExp` rather than
`std::regex`'s free functions, since the meaning is ECMA-262's.

**Flags.** ECMA-262's, as a string, and nothing else.

**Edition and Unicode version.** By default a pattern is read with today's grammar (`edition::current`) and V8's
version of Unicode (`unicode_version::v8`, 17 today). A `regex::options` overrides either, independently, beside the
match timeout: `edition::es2020` is the grammar JSON Schema 2020-12 names, and the versions are `v13`, `v17`, `v18` and
`latest`. Both are resolved when the pattern is compiled.

**Errors.** Failures throw `regex_error`, a `std::system_error` whose `reason()` is an `errc` (`invalid_pattern`,
`invalid_flags`, `pattern_too_large`, `invalid_utf8`, `match_limit`), in `ecma262::category()`. `regex::try_compile`
sets a `std::error_code` in place of throwing.

**ECMA-262 algorithms.** `replace` substitutes as `GetSubstitution` does, replacing every match only with `g`;
`replace_all` always does. `split` follows `RegExp.prototype[Symbol.split]`, with `nullopt` for a group that did not take
part.

**Input.** The type of the text chooses the encoding: `char` and `char8_t` text is UTF-8, checked before it is read and
refused when it is not, never read as U+FFFD; `char16_t` text (and `wchar_t` on Windows) is UTF-16, lone surrogates and
all. Any string view, string or pointer to a NUL-terminated string of those is taken. Positions are in the text's
units, and nothing is transcoded.

**Allocation.** `is_match`, `find`, the `find_all` range and `captures_read` (the groups into a span the caller gives)
allocate nothing once the thread has read a match with as many groups; `captures`, `replace` and `split` allocate what
they return.

**Limits.** The C library's: a match timeout per regex (a `std::chrono::milliseconds`) or for every regex
(`regex::set_default_match_timeout`), with overloads that give one match a limit of its own; the size limits; and no
stack overflow.

## 3. Engine

The engine is the Rust crate's, which
[its design](https://github.com/corvus-dotnet/corvus-regex-ecma262-rs/blob/main/docs/design.md) describes, reached
through the C library ([its design](https://github.com/corvus-dotnet/corvus-regex-ecma262-rs/blob/main/docs/design-c.md)).
The header's work is between C++ and the C library:

- **Handles.** A `regex` holds the C library's compiled pattern in a `shared_ptr`, so copying one shares the pattern,
  and any number of threads use one at once. A `captures` owns a C handle reused from match to match.
- **Text.** A string view's pointer and length go to the C library as they are; UTF-16 is passed as its code units.
- **The global search.** `find_all` reads matches from the C library 256 at a time into a buffer of its own, so the
  range's loop has no call across the boundary for each match. The range keeps the regex alive, but not the text.
- **Errors.** A status other than `CRE_OK` is thrown as `regex_error`, with the C library's message and, for text that
  is not UTF-8, the offset of the first invalid byte.

## 4. The C library

The build takes the C library from `find_package(corvus_regex_ecma262)`, or, when that finds none, downloads the C
library's release archive for the platform (`cmake/FetchCLibrary.cmake`, with each archive's hash). Two targets link
it: `corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp` (its shared library) and `..._static`. A vcpkg port and a
Conan recipe give the same targets. The API reference (`docs/api.md`) is generated from the header by
`tools/docs/gen-api.ps1`, which CI checks is current.

## 5. Targets and layout

C++20 (GCC 11, Clang 16, Apple Clang 15, MSVC 19.30 and later) and CMake 3.24, on the platforms the C library has
archives for: Linux with glibc or musl, macOS and Windows, each on x64 and arm64. The library's version is the C
library's.

- `include/corvus/regex_ecma262.hpp`: the library; `cmake/`: the package and the C library's download.
- `tests/`: the tests, the getting-started page's programs and a consumer of the installed package; `examples/`: the
  README's example; `ports/` and `conanfile.py`: the vcpkg port and the Conan recipe.
- `tools/dataset`: the test-data runner; `tools/bench`: the benchmark harness; `tools/docs`: the API reference's
  generator.

## 6. Testing

**The ECMA-262 test data.** [`ecma262-regex-test-data`](https://github.com/corvus-dotnet/ecma262-regex-test-data)
holds V8's answers: validity under each grammar, `test` answers, the positions of `exec`, the global search, `y`,
`replace` and `split`, and every `exec` call of test262. `tools/dataset` runs all of it through the header, over UTF-8
and UTF-16, with none wrong, refused or at a limit, on Linux and Windows on every change, and on Linux as the data's own
scripts write it for Unicode 13 and 18 and for ECMAScript 2020.

**Other tests.** The tests, once over the C library's shared library and once over its static one, with every warning
an error; the getting-started page's programs and the README's example; a consumer built against the installed package;
the vcpkg port and the Conan recipe built and consumed; on Linux (GCC, Clang, arm64, musl), macOS and Windows (x64 and
arm64).

## 7. Benchmarks

The harness image, `ghcr.io/corvus-dotnet/corvus-regex-ecma262-cpp`, runs the Industry workloads of
`dotnet/performance` over this library (UTF-8 and UTF-16), `std::regex`, Boost.Regex, SRELL, RE2 and V8. The published
figures are on [the benchmark page](https://corvus-oss.org/corvus-regex-ecma262-benchmarks/), which runs every engine's
image each night; this repository publishes none of its own.

Three rows are not like for like with an engine that is not ECMA-262's, as the price of its rules under u: `.*` (`.`
excludes CR, so the CRLF text has more matches), `Sher[a-z]+|Hol[a-z]+` under i (the search must also find the long s
and the Kelvin sign), and `[a-q][^u-z]{13}x` (a supplementary character is two units, so the distance to the `x` is
not fixed).

## 8. Differences a C++ user will meet

- The grammar is today's ECMA-262, not `std::regex`'s ECMAScript 3: lookbehind, named groups, Unicode properties and
  the `u`, `v`, `s`, `y` and `d` flags.
- Text is read by character, in the encoding its type names, and text that is not UTF-8 is refused.
- No pattern can overflow the stack, as `std::regex` can on a long text.

The [getting-started page](getting-started.md) has the full list.

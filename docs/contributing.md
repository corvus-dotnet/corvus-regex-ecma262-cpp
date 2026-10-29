# Contributing

This guide is for someone who wants to build the library, run its tests and gates, and open a pull request. It covers
the tools, the build step by step, the gates every change must pass, and how the library is released. The
[design document](design.md) describes what is underneath.

## 1. Tools

| Tool | Needed for | Version |
|---|---|---|
| A C++20 compiler | Everything | GCC 11, Clang 16, Apple Clang 15 or MSVC 19.30 and later |
| CMake | The build, the tests and the C library's download | 3.24 or later |
| Git | The repository and the test data | Any recent version |
| PowerShell 7 | The API reference's generator | 7.4 or later |
| Node.js | The test data for other versions of Unicode and for ECMAScript 2020, and the V8 rows of the harness | 24 (the V8 it carries is the oracle) |

- **Windows:** Visual Studio 2022 or its Build Tools with the C++ workload; `winget install Kitware.CMake Git.Git
  Microsoft.PowerShell OpenJS.NodeJS.LTS`.
- **Linux:** GCC 11 or Clang 16 and CMake from the distribution (or CMake from cmake.org); PowerShell from Microsoft's
  feed; Node.js 24 from nodejs.org.
- **macOS:** the Xcode command line tools; `brew install cmake git powershell node@24`.

The build downloads the C library's release archive for the platform, checked against its hash, unless
`CMAKE_PREFIX_PATH` names an unpacked one (`-DCORVUS_REGEX_ECMA262_FETCH=OFF` forbids the download).

## 2. Getting the sources

```
git clone https://github.com/corvus-dotnet/corvus-regex-ecma262-cpp.git
git clone https://github.com/corvus-dotnet/ecma262-regex-test-data.git   # beside it: the test data
cd corvus-regex-ecma262-cpp
```

The test data is the oracle for the engine. It is not vendored; the commands below assume it at
`../ecma262-regex-test-data`.

## 3. Building and testing, step by step

Every command runs from the repository root.

1. **Build and test.**

   ```
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release
   ctest --test-dir build -C Release --output-on-failure
   ```

   The tests run once over the C library's shared library and once over its static one, with every warning an error,
   and the getting-started page's programs and the README's example run with them.
2. **The installed package.** `cmake --install build --prefix <a directory>`, then build `tests/consumer` against it
   with that directory on `CMAKE_PREFIX_PATH`, and run it; its output must be `examples/expected.txt`.
3. **The API reference.** After a change to the header's declarations or comments, run `pwsh tools/docs/gen-api.ps1`;
   `pwsh tools/docs/gen-api.ps1 -Check` must pass.
4. **The test data.**

   ```
   cmake -S . -B build-tools -DCMAKE_BUILD_TYPE=Release -DCRE_CPP_TOOLS=ON -DCORVUS_REGEX_ECMA262_CPP_TESTS=OFF
   cmake --build build-tools --config Release --target dataset
   build-tools/tools/dataset ../ecma262-regex-test-data/data wrong-cases.tsv
   ```

   It must report 0 wrong, 0 refused and 0 at a limit. For the other versions of Unicode and for ECMAScript 2020,
   write the data with its own scripts and run it with the library set to match:

   ```
   node ../ecma262-regex-test-data/unicode.js apply 13.0 /tmp/u13 ../ecma262-regex-test-data/data
   build-tools/tools/dataset /tmp/u13 wrong-u13.tsv --unicode 13
   node ../ecma262-regex-test-data/edition.js apply /tmp/es2020 "" ../ecma262-regex-test-data/data
   build-tools/tools/dataset /tmp/es2020 wrong-es2020.tsv --edition es2020
   ```

## 4. The C library, vcpkg and Conan

The library's version is the C library's: `CMakeLists.txt` names it, and `cmake/FetchCLibrary.cmake` the hash of each
of that release's archives. Moving to a new C library release moves both, with the header's version macros. A fix to
the engine's answers is made in [the C library's repository](https://github.com/corvus-dotnet/corvus-regex-ecma262-rs)
and taken here by moving the version.

The vcpkg port (`ports/corvus-regex-ecma262-cpp`, for `--overlay-ports`) names its release's source by its hash, and
the Conan recipe (`conanfile.py`) builds with `conan create .`; CI builds a consumer with each on Linux, macOS and
Windows.

## 5. Benchmarks

`tools/bench/run.sh <workloads.json> <texts directory> <out.json>` runs this library, `std::regex`, Boost.Regex, SRELL,
RE2 and V8 over the Industry workloads; the script lists what it needs and its settings. The published figures are on
[the benchmark page](https://corvus-oss.org/corvus-regex-ecma262-benchmarks/), which runs this repository's harness
image each night beside every other engine; a run on another machine is comparable with itself, never with the page.

`std::regex`'s and SRELL's compliance is worked out once (`OTHER_ENGINES_COMPLIANCE=1`) and checked in as
`tools/bench/compliance-<engine>.json`, with the commit of the test data it was made at; CI fails when that commit is
not the one it checks out.

## 6. What a pull request needs

- **The gates of section 3** passing on the change as pushed, on every compiler CI builds with.
- **A fix to the engine's answers in the C library's repository**, with its case in the test data first.
- **A failing test first** for a bug in the header: the test that reproduces it, then the fix.
- **The design document kept current**, describing the library as it is.
- **Comments** on every public declaration of the header, since the API reference is generated from them.
- **The getting-started page** kept true: its programs are built and run by the tests.

### Conventions

- C++20, header-only, every warning an error (`-Wall -Wextra -Wpedantic -Werror`, `/W4 /WX`).
- The header adds no copy of the text and no call across the boundary for each match of a global search.

## 7. Releases

The library's version follows the C library's, so a release is made by hand: when the C library releases, a pull request
moves the version and the archives' hashes (section 4), and once it is merged a GitHub release is made, tagged
`v<version>`. A change to the benchmark harness alone is a harness release (`gh release create harness-<version>.<n>
--latest=false`), which publishes the harness image. The documentation site takes the published documents (the
getting-started page, the API reference, the design document and this guide) from `main`, and they describe the latest
release: a change to them alone needs no release, and a change to the API carries its documents and is released straight
after it is merged.

`main` is protected by a ruleset: changes arrive by pull request, and the `CI gate` and `Harness image` checks must be
green on the branch as it will merge.

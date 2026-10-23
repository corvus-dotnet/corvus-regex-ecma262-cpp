# The tools: the data-set runner (dataset) over this library and, with CRE_CPP_OTHER_ENGINES, over std::regex,
# Boost.Regex and SRELL; and, with CRE_CPP_BENCH (POSIX only, which turns CRE_CPP_OTHER_ENGINES on and needs RE2
# found by pkg-config), the Industry benchmark harness (industry). Built from the top-level project with
# -DCRE_CPP_TOOLS=ON.
enable_language(C)
find_package(Threads REQUIRED)

add_library(cre_cpp_json STATIC common/json.c)
target_include_directories(cre_cpp_json PUBLIC common)
if(NOT MSVC)
  target_compile_options(cre_cpp_json PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_executable(dataset dataset/dataset.cpp dataset/engines.cpp)
target_link_libraries(dataset PRIVATE corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp_static cre_cpp_json Threads::Threads)
if(MSVC)
  target_compile_options(dataset PRIVATE /W4 /WX /permissive- /utf-8 /bigobj /wd4100)
  target_compile_definitions(dataset PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(dataset PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
  # libstdc++'s <regex> trips GCC's maybe-uninitialized analysis in its own std::function members.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(dataset/engines.cpp PROPERTIES COMPILE_OPTIONS -Wno-maybe-uninitialized)
  endif()
endif()

if(CRE_CPP_BENCH)
  set(CRE_CPP_OTHER_ENGINES ON)
endif()

if(CRE_CPP_OTHER_ENGINES)
  include(FetchContent)
  # Boost.Regex, header-only in its standalone mode, and SRELL, each at a pinned release.
  set(CRE_CPP_BOOST_VERSION 1.87.0)
  FetchContent_Declare(boost_regex
    URL https://github.com/boostorg/regex/archive/refs/tags/boost-${CRE_CPP_BOOST_VERSION}.tar.gz
    URL_HASH SHA256=2d36fffeae36782933cae4507bd166ed1d8f42701f5694e96e1f42ccb1879137
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SOURCE_SUBDIR no-cmake-here)
  FetchContent_Declare(srell
    URL https://www.akenotsuki.com/misc/srell/releases/srell2026_07.zip
    URL_HASH SHA256=d384feae20c66b4d65bc240ee2a851aed26ca24dc4d73503eee3e81e238ec20e
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    SOURCE_SUBDIR no-cmake-here)
  FetchContent_MakeAvailable(boost_regex srell)
  target_include_directories(dataset SYSTEM PRIVATE ${boost_regex_SOURCE_DIR}/include ${srell_SOURCE_DIR})
  target_compile_definitions(dataset PRIVATE CRE_CPP_HAVE_BOOST CRE_CPP_HAVE_SRELL)
endif()

if(CRE_CPP_BENCH)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(RE2 REQUIRED IMPORTED_TARGET re2)
  add_executable(industry bench/industry.c bench/engines.cpp)
  target_include_directories(industry PRIVATE bench ${boost_regex_SOURCE_DIR}/include ${srell_SOURCE_DIR})
  target_link_libraries(industry PRIVATE corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp_static cre_cpp_json
                        PkgConfig::RE2 Threads::Threads m)
  target_compile_definitions(industry PRIVATE CRE_CPP_RE2_VERSION="${RE2_VERSION}"
                             CRE_CPP_BOOST_VERSION="${CRE_CPP_BOOST_VERSION}")
  target_compile_options(industry PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(bench/engines.cpp PROPERTIES COMPILE_OPTIONS -Wno-maybe-uninitialized)
  endif()
endif()

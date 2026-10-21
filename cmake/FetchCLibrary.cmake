# Downloads the corvus-regex-ecma262 C library's release archive for the platform being built for, from the GitHub
# release of its version, checks it against the hash recorded here, and finds its CMake package. Included by the
# top-level CMakeLists.txt when find_package does not find the C library and CORVUS_REGEX_ECMA262_FETCH is on.
#
# Sets CORVUS_REGEX_ECMA262_FETCHED_DIR to the unpacked archive, which install() copies into the prefix beside the
# header, so that the installed package finds the library it was built with.

include(FetchContent)

set(_cre_version "${CORVUS_REGEX_ECMA262_C_VERSION}")
set(_cre_hash_aarch64-apple-darwin 042faf1826f672ab806069dc3e17a7fd7071b5759b30cb5f92bd1937b5a96c3d)
set(_cre_hash_aarch64-pc-windows-msvc 5fdad937c7195e6cda2fd5068590badf9c2f854eea6962d959a767e508d66be9)
set(_cre_hash_aarch64-unknown-linux-gnu ae33b7011fd431d69de56bde7784990f6dfe20fb342d52c85ec00b9237ffc924)
set(_cre_hash_aarch64-unknown-linux-musl 9a6810073cc7bc6c68a4772b08c1bafcf33ca1570e1a66f8e27cc33ed1845cc5)
set(_cre_hash_x86_64-apple-darwin 07d44ea0181c80f75236d081fb72de41e2f3000dd84949d8e66003d95dfaefa8)
set(_cre_hash_x86_64-pc-windows-msvc ded65c288b30130f5c8dcef44b640e4b7fbb08e4e4bc252920bf9faebe7a5eb7)
set(_cre_hash_x86_64-unknown-linux-gnu 0c4d496a8277c87a7d5e642e8f466a43a1a402977a76258cafd2e58222379246)
set(_cre_hash_x86_64-unknown-linux-musl 15a1d3a40f231f256a5556e7a8c8152bc285f575f5f219a84def4c6364ffb009)

# The processor: on Windows the generator's platform (-A) when there is one, on macOS the one architecture asked for.
set(_cre_processor "${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32 AND CMAKE_GENERATOR_PLATFORM)
  set(_cre_processor "${CMAKE_GENERATOR_PLATFORM}")
elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
  list(LENGTH CMAKE_OSX_ARCHITECTURES _cre_archs)
  if(_cre_archs GREATER 1)
    message(FATAL_ERROR "corvus-regex-ecma262: the C library's archives are for one architecture each; build for one "
                        "of ${CMAKE_OSX_ARCHITECTURES}, or give find_package a C library built for all of them.")
  endif()
  set(_cre_processor "${CMAKE_OSX_ARCHITECTURES}")
endif()
string(TOLOWER "${_cre_processor}" _cre_processor)
if(_cre_processor MATCHES "^(x86_64|amd64|x64)$")
  set(_cre_arch x86_64)
elseif(_cre_processor MATCHES "^(aarch64|arm64)$")
  set(_cre_arch aarch64)
else()
  message(FATAL_ERROR "corvus-regex-ecma262: no release archive of the C library for the processor '${_cre_processor}'.")
endif()

if(WIN32)
  if(NOT MSVC)
    message(FATAL_ERROR "corvus-regex-ecma262: the Windows archives of the C library are built for MSVC.")
  endif()
  set(_cre_target ${_cre_arch}-pc-windows-msvc)
  set(_cre_extension zip)
elseif(APPLE)
  set(_cre_target ${_cre_arch}-apple-darwin)
  set(_cre_extension tar.gz)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # musl's dynamic loader is /lib/ld-musl-<arch>.so.1.
  file(GLOB _cre_musl "/lib/ld-musl-*.so.1")
  if(_cre_musl)
    set(_cre_target ${_cre_arch}-unknown-linux-musl)
  else()
    set(_cre_target ${_cre_arch}-unknown-linux-gnu)
  endif()
  set(_cre_extension tar.gz)
else()
  message(FATAL_ERROR "corvus-regex-ecma262: no release archive of the C library for ${CMAKE_SYSTEM_NAME}.")
endif()

set(_cre_url "https://github.com/corvus-dotnet/corvus-regex-ecma262-rs/releases/download/v${_cre_version}/corvus-regex-ecma262-${_cre_version}-${_cre_target}.${_cre_extension}")
message(STATUS "corvus-regex-ecma262: the C library ${_cre_version} for ${_cre_target}, from its release")

# SOURCE_SUBDIR names a directory the archive does not have, so FetchContent only downloads and unpacks it.
FetchContent_Declare(corvus_regex_ecma262_c
  URL "${_cre_url}"
  URL_HASH SHA256=${_cre_hash_${_cre_target}}
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  SOURCE_SUBDIR no-cmake-here)
FetchContent_MakeAvailable(corvus_regex_ecma262_c)

set(CORVUS_REGEX_ECMA262_FETCHED_DIR "${corvus_regex_ecma262_c_SOURCE_DIR}")
find_package(corvus_regex_ecma262 ${_cre_version} CONFIG REQUIRED
  PATHS "${CORVUS_REGEX_ECMA262_FETCHED_DIR}" NO_DEFAULT_PATH)

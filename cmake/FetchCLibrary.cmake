# Downloads the corvus-regex-ecma262 C library's release archive for the platform being built for, from the GitHub
# release of its version, checks it against the hash recorded here, and finds its CMake package. Included by the
# top-level CMakeLists.txt when find_package does not find the C library and CORVUS_REGEX_ECMA262_FETCH is on.
#
# Sets CORVUS_REGEX_ECMA262_FETCHED_DIR to the unpacked archive, which install() copies into the prefix beside the
# header, so that the installed package finds the library it was built with.

include(FetchContent)

set(_cre_version "${CORVUS_REGEX_ECMA262_C_VERSION}")
set(_cre_hash_aarch64-apple-darwin 321977bcb72d5f91a59392e11d741026773381ace6b542a9b724977f3b09fb37)
set(_cre_hash_aarch64-pc-windows-msvc cc743f5dd0830eecad6dbbbb86da73b2f482ceb8ddb19d44e02151ab35d1f64f)
set(_cre_hash_aarch64-unknown-linux-gnu 89a3dc7a2c1002d9c9a34ae84eebec282493d75bc6769f4396e5fd3020e2a6ba)
set(_cre_hash_aarch64-unknown-linux-musl a23b3551e3d90c14cb7694d9a4543e2c8329c191293898636082678fda7d43fc)
set(_cre_hash_x86_64-apple-darwin 484a3c40c9759bf8173282c7c9f02196bef3af5811b1a05546fc4a38aa07cd4f)
set(_cre_hash_x86_64-pc-windows-msvc df8b44e918daed133560ab57084e8271275ea44222535218f7ff0829ee47113a)
set(_cre_hash_x86_64-unknown-linux-gnu 3d9bae45a3bccbc1121ea0707eb59f08b545aac589e557c767163336a05b3c56)
set(_cre_hash_x86_64-unknown-linux-musl d63cfb5be8861914d6eefe81976da9ca738f2ea5b236a7ec7671025cd12ed5ed)

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

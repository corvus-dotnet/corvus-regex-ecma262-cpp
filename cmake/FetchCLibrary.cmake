# Downloads the corvus-regex-ecma262 C library's release archive for the platform being built for, from the GitHub
# release of its version, checks it against the hash recorded here, and finds its CMake package. Included by the
# top-level CMakeLists.txt when find_package does not find the C library and CORVUS_REGEX_ECMA262_FETCH is on.
#
# Sets CORVUS_REGEX_ECMA262_FETCHED_DIR to the unpacked archive, which install() copies into the prefix beside the
# header, so that the installed package finds the library it was built with.

include(FetchContent)

set(_cre_version "${CORVUS_REGEX_ECMA262_C_VERSION}")
set(_cre_hash_aarch64-apple-darwin 23b43154fdb8af1f832e56d23cd02f67db3985f3545061ef32c9a8917e886b1c)
set(_cre_hash_aarch64-pc-windows-msvc 563761df6cb207ce7a2fead25ae2bcef1f5cc2a32ac533b98fe324c3770f798a)
set(_cre_hash_aarch64-unknown-linux-gnu 9ab1c0659377e8da919fcb425b6975492a518ed11c0e1ae3129d5cab7172d05a)
set(_cre_hash_aarch64-unknown-linux-musl 6aede3011da6e713e7310cc6b0e13dd4d406537b4ddca07f0d022902e1f17013)
set(_cre_hash_x86_64-apple-darwin 1a189e8cf351f0a6aca5e91c73dd87889a6c99a6eb67da3771276e61400a1291)
set(_cre_hash_x86_64-pc-windows-msvc 74ce7ee37be90dd2671ba5e37b4ba4eb3d2358dd16d7116fe3126bb60ddfcc49)
set(_cre_hash_x86_64-unknown-linux-gnu d5568cdb5e5436c0a0b83c1616aabb1192198def688a8bd36bb5fb6cc8137ef1)
set(_cre_hash_x86_64-unknown-linux-musl a9741744b9c24b30c973eec9a1ec978459a68decb4a4fbc86b32cb658368f285)

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

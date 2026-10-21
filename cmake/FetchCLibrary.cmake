# Downloads the corvus-regex-ecma262 C library's release archive for the platform being built for, from the GitHub
# release of its version, checks it against the hash recorded here, and finds its CMake package. Included by the
# top-level CMakeLists.txt when find_package does not find the C library and CORVUS_REGEX_ECMA262_FETCH is on.
#
# Sets CORVUS_REGEX_ECMA262_FETCHED_DIR to the unpacked archive, which install() copies into the prefix beside the
# header, so that the installed package finds the library it was built with.

include(FetchContent)

set(_cre_version "${CORVUS_REGEX_ECMA262_C_VERSION}")
set(_cre_hash_aarch64-apple-darwin d4f20640c0254d2e1d31986405df9ea1398ed1e2c31fed35ff7a317e768eee69)
set(_cre_hash_aarch64-pc-windows-msvc 9fc1e333ec2bb406c6b1b42ccd5689396f3972302d0c9e2634385e2aa886e485)
set(_cre_hash_aarch64-unknown-linux-gnu c1435cd5829ed4fae7f7435d6f09f4ea55f53fb7068cbef360ae316b3508a2e5)
set(_cre_hash_aarch64-unknown-linux-musl f27d10a4a3af3117cb082918018f5342cae39f1de8689c3d44307d42c58298fe)
set(_cre_hash_x86_64-apple-darwin c77d8fbd3bf02458c7c7b73f6019e5a57b2cdd728185ae885a109bc1f5e02a9a)
set(_cre_hash_x86_64-pc-windows-msvc 52df9b267b1a4310eeb25e0d8e195686e47dc9cde05e1cd84230ffc5f1d9bfb5)
set(_cre_hash_x86_64-unknown-linux-gnu 2a910ffe9a96ae471da3c28bd144b31572878b9df9d0b7492f5988de5e9d3971)
set(_cre_hash_x86_64-unknown-linux-musl 24e27ba057a564bad55b32e3f9239bf7bd16b40348a48385c2661816afe83a4f)

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

# Downloads the corvus-regex-ecma262 C library's release archive for the platform being built for, from the GitHub
# release of its version, checks it against the hash recorded here, and finds its CMake package. Included by the
# top-level CMakeLists.txt when find_package does not find the C library and CORVUS_REGEX_ECMA262_FETCH is on.
#
# Sets CORVUS_REGEX_ECMA262_FETCHED_DIR to the unpacked archive, which install() copies into the prefix beside the
# header, so that the installed package finds the library it was built with.

include(FetchContent)

set(_cre_version "${CORVUS_REGEX_ECMA262_C_VERSION}")
set(_cre_hash_aarch64-apple-darwin d44b8baa59db5a13a045c0b0ce480b6e434a3d8b1cc9053625373ffc80b5bbaa)
set(_cre_hash_aarch64-pc-windows-msvc 08a9411b94ffbe93cef256c539898ef8f309be32e48035fd2ab2d84385e7d768)
set(_cre_hash_aarch64-unknown-linux-gnu bebc0379f23754b1674eed3b079d98c4d825dcda85b0af15f432a11802e2f332)
set(_cre_hash_aarch64-unknown-linux-musl b53960e72efb64692816789f98d1ed657d742ff57ee01f6bf10b502cb51d0766)
set(_cre_hash_x86_64-apple-darwin 568d56da2f2a20a91a70b7054bcd5200d6583534c961cc18abdfe98419a0cc5d)
set(_cre_hash_x86_64-pc-windows-msvc 5201c899b6a28b30184accdae6f3900bf9ddd9332a5b750f33751190b655f274)
set(_cre_hash_x86_64-unknown-linux-gnu 3b292ba2dac7828b84a0102bdf8463b5c04093f7849efab2b606e5756c39ac49)
set(_cre_hash_x86_64-unknown-linux-musl b77dbc5247693a24771978140b61d42ba3107507901e6e81d61e2e2721f6f3e3)

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

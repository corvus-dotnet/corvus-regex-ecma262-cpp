# The C++ header from its release (or, with --head, from main), over the C library's release archive for the triplet:
# the archive's shared library for a dynamic triplet, its static library (built for the C runtime the triplet links)
# for a static one. The C library is Rust, built and tested in its own repository's CI for each platform; the archive
# is checked against the hash recorded here.
set(CRE_C_VERSION 0.2.3)

# CORVUS_REGEX_ECMA262_CPP_SOURCE (passed through with VCPKG_KEEP_ENV_VARS) builds the port from a checkout instead,
# as this repository's CI does to test the port with the commit it runs on.
if(DEFINED ENV{CORVUS_REGEX_ECMA262_CPP_SOURCE})
    set(SOURCE_PATH "$ENV{CORVUS_REGEX_ECMA262_CPP_SOURCE}")
else()
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO corvus-dotnet/corvus-regex-ecma262-cpp
        REF "v${VERSION}"
        SHA512 fb3dbbdf562ca9e9561b7b749d9be36c187c78dd3646fe0af4543b316ed19ab6353d595fe79897147d733d14b1246c70cb93b539343c44ce0328dad8effadf4d
        HEAD_REF main)
endif()

if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(cre_arch x86_64)
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(cre_arch aarch64)
endif()
if(VCPKG_TARGET_IS_WINDOWS)
    set(cre_target ${cre_arch}-pc-windows-msvc)
    set(cre_extension zip)
elseif(VCPKG_TARGET_IS_OSX)
    set(cre_target ${cre_arch}-apple-darwin)
    set(cre_extension tar.gz)
else()
    set(cre_target ${cre_arch}-unknown-linux-gnu)
    set(cre_extension tar.gz)
endif()
set(cre_sha512_aarch64-apple-darwin aa6265c2ef713c92b5b8a81de798def917e1cef16f4f3acb9fadc8c39ca3dcf2fd69c5c708d224b73a751fc63d6b19f1183f8fa3ea624f6157f85e53eba5b706)
set(cre_sha512_aarch64-unknown-linux-gnu 5a3eaa7e0978092a4b278e19815bf6be5ea489d7949c417d843d155c2b336d149c3c49892695d5474e95bc7927d3ea4f0169371adb6aa78617fbae9cc7590690)
set(cre_sha512_x86_64-apple-darwin 738d06fdc72798a1c4f6cbce5530895a30a72cf9a01a5579fcd11d48c20d36e2a2c9d35f24087b2d0219bec4220c9e2023f9ffe7832e96ca551a8300497fc35a)
set(cre_sha512_x86_64-unknown-linux-gnu 50916274e8254b8e0879fcf4d1d49da254d23ac3080c65e0d518cfe281be0a75739058a792cf525bd1ee3f4840bbf74517b958a8514148c386966bfb0d7c37c4)
set(cre_sha512_aarch64-pc-windows-msvc aa758942ea18af9003e8d8401f9b3957ac88a520aece501e9326bff10809d3e5331f95add1edd9f61abb11ff32c2ff311f4e3abdb2db1d44e79415c5ee1d96af)
set(cre_sha512_x86_64-pc-windows-msvc def2db7ef39c7c39fa2072bbceae784a0aba60ce1c9d11016511b7527815bd331576c7d80fa439f08352d276e1ce37d456dbfda417d3301dccd72c63b6550fb7)

vcpkg_download_distfile(C_ARCHIVE
    URLS "https://github.com/corvus-dotnet/corvus-regex-ecma262-rs/releases/download/v${CRE_C_VERSION}/corvus-regex-ecma262-${CRE_C_VERSION}-${cre_target}.${cre_extension}"
    FILENAME "corvus-regex-ecma262-${CRE_C_VERSION}-${cre_target}.${cre_extension}"
    SHA512 ${cre_sha512_${cre_target}})
vcpkg_extract_source_archive(C_DIR ARCHIVE "${C_ARCHIVE}")

# The headers: the C++ library's and the C library's.
file(INSTALL "${SOURCE_PATH}/include/corvus" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${C_DIR}/include/corvus_regex_ecma262.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")

# The library, the same release build for both configurations (the C library has no debug build to give).
if(VCPKG_TARGET_IS_WINDOWS)
    if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
        set(cre_library corvus_regex_ecma262.dll.lib)
        foreach(dir "" "/debug")
            file(INSTALL "${C_DIR}/bin/corvus_regex_ecma262.dll" DESTINATION "${CURRENT_PACKAGES_DIR}${dir}/bin")
            file(INSTALL "${C_DIR}/lib/corvus_regex_ecma262.dll.lib" DESTINATION "${CURRENT_PACKAGES_DIR}${dir}/lib")
        endforeach()
    else()
        if(VCPKG_CRT_LINKAGE STREQUAL "static")
            set(cre_library corvus_regex_ecma262_mt.lib)
        else()
            set(cre_library corvus_regex_ecma262.lib)
        endif()
        foreach(dir "" "/debug")
            file(INSTALL "${C_DIR}/lib/${cre_library}" DESTINATION "${CURRENT_PACKAGES_DIR}${dir}/lib")
        endforeach()
    endif()
    set(cre_system_libraries "kernel32;ntdll;userenv;ws2_32;dbghelp")
else()
    if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
        if(VCPKG_TARGET_IS_OSX)
            set(cre_library libcorvus_regex_ecma262.dylib)
        else()
            set(cre_library libcorvus_regex_ecma262.so)
        endif()
    else()
        set(cre_library libcorvus_regex_ecma262.a)
    endif()
    foreach(dir "" "/debug")
        file(INSTALL "${C_DIR}/lib/${cre_library}" DESTINATION "${CURRENT_PACKAGES_DIR}${dir}/lib")
    endforeach()
    if(VCPKG_TARGET_IS_OSX)
        set(cre_system_libraries "System;c;m")
    else()
        set(cre_system_libraries "gcc_s;util;rt;pthread;m;dl;c")
    endif()
endif()

configure_file("${CMAKE_CURRENT_LIST_DIR}/corvus_regex_ecma262_cppConfig.cmake.in"
    "${CURRENT_PACKAGES_DIR}/share/corvus_regex_ecma262_cpp/corvus_regex_ecma262_cppConfig.cmake" @ONLY)
# The version rule of the CMake package (SameMinorVersion): the port's major and minor version, from its version.
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" cre_cpp_major_minor "${VERSION}")
set(cre_cpp_major "${CMAKE_MATCH_1}")
set(cre_cpp_minor "${CMAKE_MATCH_2}")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/corvus_regex_ecma262_cpp/corvus_regex_ecma262_cppConfigVersion.cmake"
    "set(PACKAGE_VERSION ${VERSION})
if(PACKAGE_FIND_VERSION VERSION_GREATER PACKAGE_VERSION OR NOT PACKAGE_FIND_VERSION_MAJOR EQUAL ${cre_cpp_major}
   OR NOT PACKAGE_FIND_VERSION_MINOR EQUAL ${cre_cpp_minor})
  set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
set(VCPKG_POLICY_SKIP_ABSOLUTE_PATHS_CHECK enabled)
set(VCPKG_POLICY_ALLOW_RESTRICTED_HEADERS enabled)

# The C++ header from its release (or, with --head, from main), over the C library's release archive for the triplet:
# the archive's shared library for a dynamic triplet, its static library (built for the C runtime the triplet links)
# for a static one. The C library is Rust, built and tested in its own repository's CI for each platform; the archive
# is checked against the hash recorded here.
set(CRE_C_VERSION 0.2.5)

# CORVUS_REGEX_ECMA262_CPP_SOURCE (passed through with VCPKG_KEEP_ENV_VARS) builds the port from a checkout instead,
# as this repository's CI does to test the port with the commit it runs on.
if(DEFINED ENV{CORVUS_REGEX_ECMA262_CPP_SOURCE})
    set(SOURCE_PATH "$ENV{CORVUS_REGEX_ECMA262_CPP_SOURCE}")
else()
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO corvus-dotnet/corvus-regex-ecma262-cpp
        REF "v${VERSION}"
        SHA512 5bcc26d729324963e983b0ff79a6289af02a0228487e33b8f565ce027e380f6427069c08db3c8cb9d31209bcc4c7231bbe91295eb42fd9cae452ce01d5c1a2f4
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
set(cre_sha512_aarch64-apple-darwin ca4e70b48a1f9b01567baefc0c7a891ada8edc8a099bacc6bf539145990fb46b776291f7631ad3bc7ba11c25748bb63fc8f75f5eedcb546011d0fefcb9ce6c3b)
set(cre_sha512_aarch64-unknown-linux-gnu 4a9a1b27299126604e31ce1e2b4590bee15917d061187edaf8a3d8fd5f50cf779c4f53de36ce4668ababbeafd570e7da92ef5db7412bbab73a14a45a06d0027c)
set(cre_sha512_x86_64-apple-darwin d874e64baf4b64006c67a3aa5c8511963973b8627e8446b06f9e296fc4f83357b6e04444e62ecda1085bb1b78fcebbdcdc9fed0d4cf2cefcf32121f4f7cbe639)
set(cre_sha512_x86_64-unknown-linux-gnu 22786aa7f59b22449e4da097b33ce61ab9b542aa24b9a3b7ab2828bb7db7262db46663b60e712b1587709135dd2f04ea26a9f74b1f27763f498a2c25c70555fb)
set(cre_sha512_aarch64-pc-windows-msvc 80ef465e7efb6bd1697151d955de7c0f5ab556dddb05fe85222ade98dab05d56228706413e8740fbf5cc475db56cc4deef8dd1e2bbc6858c365b3cb307af2a46)
set(cre_sha512_x86_64-pc-windows-msvc 505320a21355a0b65d1a87b7271b5e88f4a2b1c8f674a2648c8cef2373341672a0831587107bddce4e04457df6ab07d09329064cba251a91bbb398edf9b1ce03)

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

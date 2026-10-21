# The C++ header from its release (or, with --head, from main), over the C library's release archive for the triplet:
# the archive's shared library for a dynamic triplet, its static library (built for the C runtime the triplet links)
# for a static one. The C library is Rust, built and tested in its own repository's CI for each platform; the archive
# is checked against the hash recorded here.
set(CRE_C_VERSION 0.1.1)

# CORVUS_REGEX_ECMA262_CPP_SOURCE (passed through with VCPKG_KEEP_ENV_VARS) builds the port from a checkout instead,
# as this repository's CI does to test the port with the commit it runs on.
if(DEFINED ENV{CORVUS_REGEX_ECMA262_CPP_SOURCE})
    set(SOURCE_PATH "$ENV{CORVUS_REGEX_ECMA262_CPP_SOURCE}")
else()
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO corvus-dotnet/corvus-regex-ecma262-cpp
        REF "v${VERSION}"
        SHA512 a3a19b1c0be78b64092386a17af991eecc9531fda5bacc61ef37aa8586b0c5a9394391d53f7eecb6486c07a9425f2ed3bc9cd0ce8b664870874048cac36bb4c7
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
set(cre_sha512_aarch64-apple-darwin f440b551cf9be21753e93ccbff443c4ad7c427ca66b030eda7bbdd8d0e133fae4c06b41bdb13c56c63cccbf941d59d8df340247d49a21714463acaabc3c32b0f)
set(cre_sha512_aarch64-unknown-linux-gnu 51a69b09d670acc20c48544b5ff714c023313ed08f7909f957973012e4915261efe84893bb22858a58896c24c772f11c84621b3916e65f6f6696c9305873a9ba)
set(cre_sha512_x86_64-apple-darwin 2287e2d79e9e716e3cbd9951d638986ecfffdb66361a80a691e47c5a4902585ed0ee1ebd8fd942c4fd0e221dfbc0a611ac90ee1fc6024042c52673b1eb7f378c)
set(cre_sha512_x86_64-unknown-linux-gnu 5c0618ac80cec602a59d69fce3024f6376c107663864ca20f5bc12db648b48a3b410d6834bd6c056ba6b2aad697b43ad04b89ce8407e3ad0406d0e9f5b860136)
set(cre_sha512_aarch64-pc-windows-msvc 8686a4f8ac0129d63ec01f032b9b249684e002d39e214b39dffd620baa1820f2c9eb92657e06276b310526b9688d7899f740b1c5f5c54c742d98b2a170f5a7d8)
set(cre_sha512_x86_64-pc-windows-msvc c95d8f84c272b5e6a309ecb49a1d0ead903b0d27769c4e1d09bce3d0729c36f3480c721d87353f347a63a71a759cb3942f7a158e6187bfeaf893653ebbf5eed8)

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
file(WRITE "${CURRENT_PACKAGES_DIR}/share/corvus_regex_ecma262_cpp/corvus_regex_ecma262_cppConfigVersion.cmake"
    "set(PACKAGE_VERSION ${VERSION})
if(PACKAGE_FIND_VERSION VERSION_GREATER PACKAGE_VERSION OR NOT PACKAGE_FIND_VERSION_MAJOR EQUAL 0
   OR NOT PACKAGE_FIND_VERSION_MINOR EQUAL 1)
  set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
set(VCPKG_POLICY_SKIP_ABSOLUTE_PATHS_CHECK enabled)
set(VCPKG_POLICY_ALLOW_RESTRICTED_HEADERS enabled)

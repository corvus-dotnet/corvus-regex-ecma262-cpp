# The C++ header from its release (or, with --head, from main), over the C library's release archive for the triplet:
# the archive's shared library for a dynamic triplet, its static library (built for the C runtime the triplet links)
# for a static one. The C library is Rust, built and tested in its own repository's CI for each platform; the archive
# is checked against the hash recorded here.
set(CRE_C_VERSION 0.2.0)

# CORVUS_REGEX_ECMA262_CPP_SOURCE (passed through with VCPKG_KEEP_ENV_VARS) builds the port from a checkout instead,
# as this repository's CI does to test the port with the commit it runs on.
if(DEFINED ENV{CORVUS_REGEX_ECMA262_CPP_SOURCE})
    set(SOURCE_PATH "$ENV{CORVUS_REGEX_ECMA262_CPP_SOURCE}")
else()
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO corvus-dotnet/corvus-regex-ecma262-cpp
        REF "v${VERSION}"
        SHA512 1890aca59778a01a567704a916fea2870ee414f16f877d155634efa2519185fe40ec5a20b1ba94f6e764f8277e4e6e5a5ac7f59d1eaf1a64c9af9d0a12ebf31b
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
set(cre_sha512_aarch64-apple-darwin 02a094674df043072056d03c732f00094bacbfce99a8bc2fd8d65ac477df83d318997b7dfd860aa17ba69450e6965426c00dba2a3be89d32910a4d9cce3465bf)
set(cre_sha512_aarch64-unknown-linux-gnu 55d411d54eee734e1d751af1c39682425a266305dcbf60ee91aa5a0e636ca58436ec433abd256fe0e30c8010fcb6871578514d59f003382b1db403233e628b99)
set(cre_sha512_x86_64-apple-darwin 43e2c62ee4379897ca58a9d771850f6cd84a2e22e06cdaba1c192ae0dbd25296ca945103d3468ad7ceb6a405ba3b0fe70eea1ec68926c34cc9149dc537a1e2d2)
set(cre_sha512_x86_64-unknown-linux-gnu ae1fb40e0d282cd6d29d566e37923892fcbccccdced75849868f4452f4ec2e757f46a77233589a9200ad75193e8921834c0287015200c6fd502422f50ef4eda5)
set(cre_sha512_aarch64-pc-windows-msvc 6b966f44800f907dd2c0748c150d1efbb4988bc72a70b5cf0aeb3932aec9efe88a5899834714c01339e8061241793881f686bf9c5c2321e5dfddb7d6e5b7e72f)
set(cre_sha512_x86_64-pc-windows-msvc 1de05dc1a95fbfb10b805c9131367602f59a582c55d4fe29031bbcb210d523eb77c40ca1ef83adaadd188ed50d9a6c5a71d480ee64bf890889f754f58b9a4fc5)

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

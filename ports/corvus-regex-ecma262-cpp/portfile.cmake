# The C++ header from its release (or, with --head, from main), over the C library's release archive for the triplet:
# the archive's shared library for a dynamic triplet, its static library (built for the C runtime the triplet links)
# for a static one. The C library is Rust, built and tested in its own repository's CI for each platform; the archive
# is checked against the hash recorded here.
set(CRE_C_VERSION 0.1.3)

# CORVUS_REGEX_ECMA262_CPP_SOURCE (passed through with VCPKG_KEEP_ENV_VARS) builds the port from a checkout instead,
# as this repository's CI does to test the port with the commit it runs on.
if(DEFINED ENV{CORVUS_REGEX_ECMA262_CPP_SOURCE})
    set(SOURCE_PATH "$ENV{CORVUS_REGEX_ECMA262_CPP_SOURCE}")
else()
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO corvus-dotnet/corvus-regex-ecma262-cpp
        REF "v${VERSION}"
        SHA512 823eb7fa9ec2a7c64ccbc8f3096427a9105d2ad0f2a77a98491ab974d042025cd4125c7727a626c2f200c554ffd8a7cbd6b8045b3238a807d7a9ff63adbc4ab3
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
set(cre_sha512_aarch64-apple-darwin deb9ad15f3e78ba3f12139f59017e14a8d366f7a2897c32c9d0ffd6e4e1265a84c2c17c4540df1edeb3e00ee7f9ae6078318251c0728873bfde0108d7b23aaaa)
set(cre_sha512_aarch64-unknown-linux-gnu 76cdde9c32babe9a48fef7d7d38748daee400afdab3ae35df6005cab8f46bf8479809a17fb113a28867b0c4dbe17e14b332bdf2b51d440da4f22194c10106487)
set(cre_sha512_x86_64-apple-darwin e935ce1617d9599bdc0fd48570814c6c5ae1c1cce064905912f92985cff144486f117c3d27080236873f28cde0e1dfa9c984e48d40ad759740258a07b85f2779)
set(cre_sha512_x86_64-unknown-linux-gnu 22a9f304b7578b8b5fa9508f3e20b18647617948e5dd67217fd30eb85630e949ab82e122fe28e96dcd3d70939e72d95b22437b25aebdd784b93555a2381c650b)
set(cre_sha512_aarch64-pc-windows-msvc fae1a9bf48e48cf710f53eafafee59328ab811c356e749d65f6f209f622edc4d48e144590c7e2fdfbbbab3a2b1cd8c028899ad3d24b8a305aa138b6e68f36a5c)
set(cre_sha512_x86_64-pc-windows-msvc cfb20a9e205d280ab4c5fbc9f1d64d806865f951c75831be827d72d6b1d92a4086ad439d06b75bef45a06c6ee545569b3bfda70527d4977abaa126dc228ac51d)

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

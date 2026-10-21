# The corvus-regex-ecma262 C++ library: one header (include/corvus/regex_ecma262.hpp) over the C library, which is
# found with find_package(corvus_regex_ecma262) or, when that finds nothing and CORVUS_REGEX_ECMA262_FETCH is on,
# downloaded from the C library's release for the platform (cmake/FetchCLibrary.cmake).
#
# As a dependency of another project:
#
#   FetchContent_Declare(corvus_regex_ecma262_cpp GIT_REPOSITORY https://github.com/corvus-dotnet/corvus-regex-ecma262-cpp GIT_TAG v0.1.2)
#   FetchContent_MakeAvailable(corvus_regex_ecma262_cpp)
#   target_link_libraries(app PRIVATE corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp)
#
# or, once installed, find_package(corvus_regex_ecma262_cpp CONFIG REQUIRED) with the same target.
cmake_minimum_required(VERSION 3.24)
project(corvus_regex_ecma262_cpp VERSION 0.1.2 LANGUAGES CXX)

option(CORVUS_REGEX_ECMA262_FETCH "Download the C library's release archive when find_package does not find it" ON)
option(CORVUS_REGEX_ECMA262_CPP_TESTS "Build the tests and the examples" ${PROJECT_IS_TOP_LEVEL})
option(CORVUS_REGEX_ECMA262_CPP_INSTALL "Generate the install rules" ${PROJECT_IS_TOP_LEVEL})
option(CRE_CPP_TOOLS "Build the data-set runner (tools/)" OFF)
option(CRE_CPP_COMPETITORS "Build the tools with std::regex, Boost.Regex and SRELL beside this library" OFF)
option(CRE_CPP_BENCH "Build the Industry benchmark harness (POSIX; needs RE2) with the tools" OFF)

# The C library this header is written against: its version is this library's.
set(CORVUS_REGEX_ECMA262_C_VERSION ${PROJECT_VERSION})

if(NOT TARGET corvus_regex_ecma262::corvus_regex_ecma262)
  find_package(corvus_regex_ecma262 ${CORVUS_REGEX_ECMA262_C_VERSION} CONFIG QUIET)
  if(NOT corvus_regex_ecma262_FOUND)
    if(CORVUS_REGEX_ECMA262_FETCH)
      include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchCLibrary.cmake)
    else()
      message(FATAL_ERROR "corvus-regex-ecma262: the C library ${CORVUS_REGEX_ECMA262_C_VERSION} was not found; set "
                          "CMAKE_PREFIX_PATH to an unpacked release archive, or turn CORVUS_REGEX_ECMA262_FETCH on.")
    endif()
  endif()
endif()

add_library(corvus_regex_ecma262_cpp INTERFACE)
add_library(corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp ALIAS corvus_regex_ecma262_cpp)
target_include_directories(corvus_regex_ecma262_cpp INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(corvus_regex_ecma262_cpp INTERFACE cxx_std_20)
target_link_libraries(corvus_regex_ecma262_cpp INTERFACE corvus_regex_ecma262::corvus_regex_ecma262)

add_library(corvus_regex_ecma262_cpp_static INTERFACE)
add_library(corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp_static ALIAS corvus_regex_ecma262_cpp_static)
target_include_directories(corvus_regex_ecma262_cpp_static INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(corvus_regex_ecma262_cpp_static INTERFACE cxx_std_20)
target_link_libraries(corvus_regex_ecma262_cpp_static INTERFACE corvus_regex_ecma262::corvus_regex_ecma262_static)

if(CORVUS_REGEX_ECMA262_CPP_TESTS)
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(examples)
endif()

if(CRE_CPP_TOOLS)
  add_subdirectory(tools)
endif()

if(CORVUS_REGEX_ECMA262_CPP_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(TARGETS corvus_regex_ecma262_cpp corvus_regex_ecma262_cpp_static EXPORT corvus_regex_ecma262_cppTargets)
  install(EXPORT corvus_regex_ecma262_cppTargets
    NAMESPACE corvus_regex_ecma262_cpp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/corvus_regex_ecma262_cpp)
  configure_package_config_file(cmake/corvus_regex_ecma262_cppConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/corvus_regex_ecma262_cppConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/corvus_regex_ecma262_cpp)
  # Before 1.0 a minor version may break the API, as the C library's package says.
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/corvus_regex_ecma262_cppConfigVersion.cmake
    COMPATIBILITY SameMinorVersion ARCH_INDEPENDENT)
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/corvus_regex_ecma262_cppConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/corvus_regex_ecma262_cppConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/corvus_regex_ecma262_cpp)
  # A C library this build downloaded is installed with the header, so the installed package finds it. Its layout is
  # kept as the archive has it (include, lib, bin), since its package finds its files from where it is.
  if(CORVUS_REGEX_ECMA262_FETCHED_DIR)
    install(DIRECTORY ${CORVUS_REGEX_ECMA262_FETCHED_DIR}/include/ DESTINATION include)
    install(DIRECTORY ${CORVUS_REGEX_ECMA262_FETCHED_DIR}/lib/ DESTINATION lib)
    if(EXISTS ${CORVUS_REGEX_ECMA262_FETCHED_DIR}/bin)
      install(DIRECTORY ${CORVUS_REGEX_ECMA262_FETCHED_DIR}/bin/ DESTINATION bin)
    endif()
    install(FILES ${CORVUS_REGEX_ECMA262_FETCHED_DIR}/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/corvus-regex-ecma262
      RENAME LICENSE.c-library)
  endif()
endif()

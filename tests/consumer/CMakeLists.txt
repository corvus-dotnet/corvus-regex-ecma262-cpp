# A project that uses the installed package as a consumer does: find_package, then the example.
cmake_minimum_required(VERSION 3.24)
project(consumer CXX)
find_package(corvus_regex_ecma262_cpp 0.2 CONFIG REQUIRED)
add_executable(consumer ../../examples/basic.cpp)
target_link_libraries(consumer PRIVATE corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp)
if(MSVC)
  target_compile_options(consumer PRIVATE /utf-8)
  add_custom_command(TARGET consumer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:consumer> $<TARGET_FILE_DIR:consumer>
    COMMAND_EXPAND_LISTS)
endif()

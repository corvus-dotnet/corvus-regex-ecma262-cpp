# The README's example, built and run, its output compared with expected.txt.
add_executable(example_basic basic.cpp)
target_link_libraries(example_basic PRIVATE corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp_static)
if(MSVC)
  target_compile_options(example_basic PRIVATE /utf-8)
endif()
add_test(NAME example_basic
  COMMAND ${CMAKE_COMMAND} -DPROGRAM=$<TARGET_FILE:example_basic> -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected.txt
          -P ${CMAKE_CURRENT_SOURCE_DIR}/compare.cmake)

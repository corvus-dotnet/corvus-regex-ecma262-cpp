# The tests, once over the C library's shared library and once over its static library.
if(MSVC)
  set(cre_cpp_warnings /W4 /WX /permissive- /utf-8)
else()
  set(cre_cpp_warnings -Wall -Wextra -Wpedantic -Werror)
endif()
find_package(Threads REQUIRED)

foreach(variant IN ITEMS shared static)
  if(variant STREQUAL "shared")
    set(cre_cpp_target corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp)
  else()
    set(cre_cpp_target corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp_static)
  endif()
  add_executable(tests_${variant} tests.cpp)
  target_link_libraries(tests_${variant} PRIVATE ${cre_cpp_target} Threads::Threads)
  target_compile_options(tests_${variant} PRIVATE ${cre_cpp_warnings})
  add_test(NAME tests_${variant} COMMAND tests_${variant})
endforeach()

# On Windows the C library's DLL is found next to the program.
if(WIN32)
  add_custom_command(TARGET tests_shared POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:tests_shared> $<TARGET_FILE_DIR:tests_shared>
    COMMAND_EXPAND_LISTS)
endif()

# The C++ programs of the getting-started page the documentation site publishes (docs/getting-started.md): each block
# of C++ with a main is a program of its own, built with the tests' warnings and with assert on, and run, so that the
# page shows only code that works.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/docs/getting-started.md")
file(READ "${PROJECT_SOURCE_DIR}/docs/getting-started.md" doc)
set(n 0)
while(TRUE)
  string(FIND "${doc}" "```cpp\n" begin)
  if(begin EQUAL -1)
    break()
  endif()
  math(EXPR begin "${begin} + 7")
  string(SUBSTRING "${doc}" ${begin} -1 doc)
  string(FIND "${doc}" "\n```" end)
  string(SUBSTRING "${doc}" 0 ${end} code)
  math(EXPR end "${end} + 4")
  string(SUBSTRING "${doc}" ${end} -1 doc)
  # A block that is not a whole program, such as the #include of Setup, is not built.
  string(FIND "${code}" "int main(" has_main)
  if(has_main EQUAL -1)
    continue()
  endif()
  math(EXPR n "${n} + 1")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/getting_started_${n}.cpp" "${code}\n")
  add_executable(getting_started_${n} "${CMAKE_CURRENT_BINARY_DIR}/getting_started_${n}.cpp")
  target_link_libraries(getting_started_${n} PRIVATE corvus_regex_ecma262_cpp::corvus_regex_ecma262_cpp_static)
  target_compile_options(getting_started_${n} PRIVATE ${cre_cpp_warnings} $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
  add_test(NAME getting_started_${n} COMMAND getting_started_${n})
endwhile()
if(n LESS 5)
  message(FATAL_ERROR "docs/getting-started.md has ${n} programs of C++.")
endif()

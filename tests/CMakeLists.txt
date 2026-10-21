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

# SPDX-License-Identifier: GPL-2.0-or-later
#
# Setup for unit tests.
add_custom_target(unit_tests)

# Add a unit test as follows:
# add_unit_test(name-of-my-test SOURCES foo.cpp ...)
#
# Note that the main test file must exist as testfiles/src/name-of-my-test.cpp
# Additional sources are given after SOURCES; their paths are relative to Inkscape's src/
# and are meant to be limited to only the source files containing the tested functionality.
#
# You can also add the argument EXTRA_LIBS followed by additional libraries you would like
# to be linked to your test. This is not recommended unless you explicitly intend to test
# integrations with the functionality provided by those libraries.
function(add_unit_test test_name)
    set(MULTI_VALUE_ARGS "SOURCES" "EXTRA_LIBS")
    cmake_parse_arguments(ARG "UNUSED_OPTIONS" "UNUSED_MONOVAL" "${MULTI_VALUE_ARGS}" ${ARGN})
    foreach(source_file ${ARG_SOURCES})
        list(APPEND test_sources ${CMAKE_SOURCE_DIR}/src/${source_file})
    endforeach()

    add_executable(${test_name} src/${test_name}.cpp ${test_sources})
    target_include_directories(${test_name} SYSTEM PRIVATE ${GTEST_INCLUDE_DIRS})

    target_compile_definitions(${test_name} PRIVATE "-D_GLIBCXX_ASSERTIONS")
    target_compile_options(${test_name} PRIVATE "-fsanitize=address" "-fno-omit-frame-pointer" "-UNDEBUG")
    target_link_options(${test_name} PRIVATE "-fsanitize=address")

    target_link_libraries(${test_name} GTest::gtest GTest::gmock GTest::gmock_main ${ARG_EXTRA_LIBS})
    add_test(NAME ${test_name} COMMAND ${test_name})
    add_dependencies(unit_tests ${test_name} ${ARG_EXTRA_LIBS})
endfunction(add_unit_test)


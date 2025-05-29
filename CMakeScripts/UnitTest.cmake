# SPDX-License-Identifier: GPL-2.0-or-later
#
# Setup for unit tests.
add_custom_target(unit_tests)

# Add a unit test as follows:
# add_unit_test(name-of-my-test TEST_SOURCE foo-test.cpp [SOURCES foo.cpp ...] [EXTRA_LIBS ...])
function(add_unit_test test_name)
    set(MULTI_VALUE_ARGS "SOURCES" "EXTRA_LIBS")
    cmake_parse_arguments(ARG "UNUSED_OPTIONS" "TEST_SOURCE" "${MULTI_VALUE_ARGS}" ${ARGN})
    foreach(source_file ${ARG_SOURCES})
        list(APPEND test_sources "${CMAKE_SOURCE_DIR}/src/${source_file}")
    endforeach()
    if(EXISTS "${CMAKE_SOURCE_DIR}/testfiles/src/${test_name}.cpp")
        list(APPEND test_sources "${CMAKE_SOURCE_DIR}/testfiles/src/${test_name}.cpp")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/testfiles/src/${TEST_SOURCE}")
        list(APPEND test_sources "${CMAKE_SOURCE_DIR}/testfiles/src/${TEST_SOURCE}")
    endif()

    add_executable(${test_name} ${test_sources})
    target_include_directories(${test_name} SYSTEM PRIVATE ${GTEST_INCLUDE_DIRS})
    set_target_properties(${test_name} PROPERTIES LINKER_LANGUAGE CXX)

    target_compile_definitions(${test_name} PRIVATE "-D_GLIBCXX_ASSERTIONS")
    target_compile_options(${test_name} PRIVATE "-fsanitize=address" "-fno-omit-frame-pointer" "-UNDEBUG")
    target_link_options(${test_name} PRIVATE "-fsanitize=address")

    target_link_libraries(${test_name} GTest::gtest GTest::gmock GTest::gmock_main ${ARG_EXTRA_LIBS})
    add_test(NAME ${test_name} COMMAND ${test_name})
    add_dependencies(unit_tests ${test_name} ${ARG_EXTRA_LIBS})
endfunction(add_unit_test)


# SPDX-License-Identifier: GPL-2.0-or-later
# -----------------------------------------------------------------------------

# custom "check" target with proper dependencies (builds inkscape and tests)
file(TO_NATIVE_PATH "/" _separator)
ADD_DEFINITIONS(-DINKSCAPE_TESTS_DIR="${CMAKE_SOURCE_DIR}/testfiles")
add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
                        DEPENDS tests unit_tests
                        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_dependencies(check inkscape)

# create symlink "inkscape_datadir" to use as INKSCAPE_DATADIR
# - ensures tests can be run without installing the project
# - also helpful for running Inkscape uninstalled: 'INKSVAPE_DATADIR=inkscape_datadir bin/inkscape'
set(INKSCAPE_DATADIR ${CMAKE_BINARY_DIR}/inkscape_datadir)
if(NOT EXISTS ${INKSCAPE_DATADIR}/inkscape)
    set(link_source ${INKSCAPE_DATADIR}/inkscape)
    set(link_target ${CMAKE_SOURCE_DIR}/share)
    message(STATUS "Creating link '${link_source}' --> '${link_target}'")
    execute_process(COMMAND mkdir inkscape_datadir)
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${link_target} ${link_source}
                    RESULT_VARIABLE result)
    if(result)
        message(WARNING "Creation of link failed: ${result}")
    endif()
endif()
# check if creation succeeded
if(EXISTS ${INKSCAPE_DATADIR}/inkscape)
    set(CMAKE_CTEST_ENV INKSCAPE_DATADIR=${INKSCAPE_DATADIR})
else()
    message(WARNING "Directory 'inkscape_datadir/inkscape' missing. Tests might not run properly.\n"
                    "Possible solutions:\n"
                    " - create a suitable symlink yourself, e.g.\n"
                    "   ln -s ${CMAKE_SOURCE_DIR}/share ${INKSCAPE_DATADIR}/inkscape\n"
                    " - run '${CMAKE_MAKE_PROGRAM} install' before running tests (only for not relocatable packages.\n"
                    " - set the environment variable 'INKSCAPE_DATADIR' manually (every time you run tests)")
endif()


# Set custom profile directory for tests using environment variable.
# Copy CTestCustom.cmake into binary dir, where it will be picked up automatically by ctest for cleanup.
set(INKSCAPE_TEST_PROFILE_DIR ${CMAKE_CURRENT_BINARY_DIR}/test_profile_dir)
set(INKSCAPE_TEST_PROFILE_DIR_ENV INKSCAPE_PROFILE_DIR=${INKSCAPE_TEST_PROFILE_DIR})
configure_file(CTestCustom.cmake.in ${CMAKE_BINARY_DIR}/CTestCustom.cmake)

### tests using gtest
include_directories("${CMAKE_SOURCE_DIR}/src/3rdparty/adaptagrams") # TODO: remove this hack

if(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
    set(LPE_TESTS_64bit
        #0.92 or lower LPEs
        # (test not stable on 32bit Windows)
        lpe64-test
    )
endif()

# Integration tests (linking all of Inkscape)
set(TEST_SOURCES
    actions-svg-processing
    async_channel-test
    async_funclog-test
    async_progress-test
    boolop-attr-test
    colors/cms-test
    colors/color-test
    colors/document-cms-test
    colors/dragndrop-test
    colors/gamut-test
    colors/manager-test
    colors/parser-test
    colors/printer-test
    colors/color-set-test
    colors/spaces-cms-test
    colors/spaces-cmyk-test
    colors/spaces-gray-test
    colors/spaces-hsl-test
    colors/spaces-hsluv-test
    colors/spaces-hsv-test
    colors/spaces-lab-test
    colors/spaces-lch-test
    colors/spaces-linear-rgb-test
    colors/spaces-luv-test
    colors/spaces-named-test
    colors/spaces-okhsl-test
    colors/spaces-okhsv-test
    colors/spaces-oklab-test
    colors/spaces-oklch-test
    colors/spaces-rgb-test
    colors/spaces-xyz-test
    colors/utils-test
    colors/xml-color-test
    uri-test
    util-test
    drag-and-drop-svgz
    drawing-pattern-test
    poppler-utils-test
    extract-uri-test
    attributes-test
    dir-util-test
    sp-item-test
    sp-object-test
    sp-object-tags-test
    object-links-test
    object-set-test
    object-style-test
    page-management
    path-boolop-test
    path-reverse-lpe-test
    preferences-test
    rebase-hrefs-test
    stream-test
    style-elem-test
    style-internal-test
    style-test
    svg-affine-test
    svg-box-test
    svg-length-test
    svg-stringstream-test
    sp-gradient-test
    svg-path-geom-test
    visual-bounds-test
    geom-pathstroke-test
    livarot-pathoutline-test
    object-test
    sp-glyph-kerning-test
    cairo-utils-test
    svg-extension-test
    curve-test
    2geom-characterization-test
    xml-test
    sp-item-group-test
    store-test
    lpe-test
    ui-util-test
    ${LPE_TESTS_64bit}
    )

if(WITH_CAPYPDF)
    list(APPEND TEST_SOURCES
        extensions-pdfoutput-test
    )
endif()

add_library(cpp_test_static_library SHARED inkscape-test.cpp doc-per-case-test.cpp compare-paths-test.h lpespaths-test.h store-integrity-test.h test-with-svg-object-pairs.cpp)
target_link_libraries(cpp_test_static_library PUBLIC ${GTEST_LIBRARIES} inkscape_base)

add_custom_target(tests)
foreach(test_source ${TEST_SOURCES})
    string(REPLACE "-test" "" testname "test_${test_source}")
    string(REPLACE "/" "_" testname "${testname}")
    add_executable(${testname} src/${test_source}.cpp)
    target_include_directories(${testname} SYSTEM PRIVATE ${GTEST_INCLUDE_DIRS})
    target_link_libraries(${testname} cpp_test_static_library 2Geom::2geom)
    add_test(NAME ${testname} COMMAND ${testname})
    set_tests_properties(${testname} PROPERTIES ENVIRONMENT "${INKSCAPE_TEST_PROFILE_DIR_ENV}/${testname};${CMAKE_CTEST_ENV}")
    add_dependencies(tests ${testname})
endforeach()
include(${CMAKE_SOURCE_DIR}/CMakeScripts/UnitTest.cmake)

### Unit tests
#
# In order to add a unit test, call add_unit_test() providing a unique name for the test,
# and then the following named parameters:
# TEST_SOURCE followed by the file containing the test code (relative to testfiles/src)
# SOURCES     followed by a list of files containing the code under test (relative to src/)
# EXTRA_LIBS  followed by libraries which you need to be linked to the test.
#
add_unit_test(assertions-in-tests TEST_SOURCE "assertions-in-tests.cpp")
add_unit_test(version-test TEST_SOURCE "version-test.cpp"
                               SOURCES "version.cpp")
add_unit_test(css-syntactic-decomposition-test TEST_SOURCE "css-syntactic-decomposition-test.cpp"
                                                   SOURCES "css/syntactic-decomposition.cpp"
                                                EXTRA_LIBS croco_LIB)
add_unit_test(xslt-extension-test TEST_SOURCE "xslt-extension-test.cpp"
                                      SOURCES "extension/implementation/xslt.cpp"
                                   EXTRA_LIBS 2Geom::2geom)
add_unit_test(mod360-test TEST_SOURCE "mod360-test.cpp"
                              SOURCES "mod360.cpp"
                           EXTRA_LIBS 2Geom::2geom)

add_dependencies(tests unit_tests)

### CLI rendering tests and LPE
add_subdirectory(cli_tests)
add_subdirectory(rendering_tests)
add_subdirectory(lpe_tests)

### Fuzz test
if(WITH_FUZZ)
    # to use the fuzzer, make sure you use the right compiler (clang)
    # with the right flags -fsanitize=address -fsanitize-coverage=edge,trace-pc-guard,indirect-calls,trace-cmp,trace-div,trace-gep -fno-omit-frame-pointer
    # (see libfuzzer doc for info in flags)
    # first line is for integration into oss-fuzz https://github.com/google/oss-fuzz
    add_executable(fuzz fuzzer.cpp)
    if(LIB_FUZZING_ENGINE)
        target_link_libraries(fuzz inkscape_base -lFuzzingEngine)
    else()
        target_link_libraries(fuzz inkscape_base -lFuzzer)
    endif()
endif()

find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

set(QTRANS_TEST_DIRS
    task
    storage
    network
    model
    translation
    text
    log
)

foreach(test_dir IN LISTS QTRANS_TEST_DIRS)
    file(GLOB _sources CONFIGURE_DEPENDS "${test_dir}/*.cpp")
    if(NOT _sources)
        continue()
    endif()

    add_executable(qtrans_tests_${test_dir} ${_sources})
    target_link_libraries(
        qtrans_tests_${test_dir}
        PRIVATE
            qtrans_engine
            GTest::gtest
            GTest::gtest_main
    )

    if(MSVC)
        target_compile_options(qtrans_tests_${test_dir} PRIVATE /W4)
    else()
        target_compile_options(qtrans_tests_${test_dir} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    gtest_discover_tests(qtrans_tests_${test_dir}
        PROPERTIES
            LABELS "dir:${test_dir}"
    )
endforeach()

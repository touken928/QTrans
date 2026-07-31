find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(qtrans_tests_core
    core/test_core_api.cpp
    core/test_model_host.cpp
)
target_link_libraries(qtrans_tests_core
    PRIVATE
        qtrans_core
        GTest::gtest
        GTest::gtest_main
)
target_compile_options(qtrans_tests_core PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(qtrans_tests_core PRIVATE ${PROJECT_SOURCE_DIR}/src/core/internal/host)
gtest_discover_tests(qtrans_tests_core
    PROPERTIES
        LABELS "dir:core"
)

add_executable(qtrans_tests_core_internal
    core/test_local_runtime_lifecycle.cpp
)
target_link_libraries(qtrans_tests_core_internal
    PRIVATE
        qtrans_core
        GTest::gtest
        GTest::gtest_main
)
target_compile_options(qtrans_tests_core_internal PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(qtrans_tests_core_internal PRIVATE ${PROJECT_SOURCE_DIR}/src/core/internal)
gtest_discover_tests(qtrans_tests_core_internal
    PROPERTIES
        LABELS "dir:core"
)

add_test(
    NAME CoreInstallSmoke
    COMMAND ${CMAKE_COMMAND}
        -DQTRANS_BUILD_DIR=${CMAKE_BINARY_DIR}
        -DQTRANS_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DQTRANS_DEPENDENCY_PREFIX=${CMAKE_PREFIX_PATH}
        -P ${CMAKE_SOURCE_DIR}/cmake/QTransCoreInstallSmoke.cmake
)

set(QTRANS_TEST_DIRS
    batch
    services
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
            qtrans_desktop
            GTest::gtest
            GTest::gtest_main
    )

    if(MSVC)
        target_compile_options(qtrans_tests_${test_dir} PRIVATE /W4)
    else()
        target_compile_options(qtrans_tests_${test_dir} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(test_dir STREQUAL "services")
        target_include_directories(qtrans_tests_${test_dir} PRIVATE ${PROJECT_SOURCE_DIR}/src/core/internal/host)
    endif()

    gtest_discover_tests(qtrans_tests_${test_dir}
        PROPERTIES
            LABELS "dir:${test_dir}"
    )
endforeach()

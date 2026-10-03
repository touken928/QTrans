find_package(GTest CONFIG REQUIRED)
include(GoogleTest)
include(${PROJECT_SOURCE_DIR}/src/sources.cmake)

function(qtrans_configure_test_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(qtrans_configure_runtime_test target)
    target_link_libraries(${target}
        PRIVATE
            llama-cpp::llama
            simdutf::simdutf
            spdlog::spdlog
            sentbreak::sentbreak
            GTest::gtest
            GTest::gtest_main
    )
    target_include_directories(${target}
        PRIVATE
            ${QTRANS_RUNTIME_INCLUDE_DIRS}
            ${QTRANS_RUNTIME_INTERNAL_INCLUDE_DIRS}
    )
    qtrans_link_llama_openmp(${target})
    qtrans_apply_project_definitions(${target})
    qtrans_configure_test_warnings(${target})
endfunction()

function(qtrans_configure_app_test target)
    target_link_libraries(${target}
        PRIVATE
            llama-cpp::llama
            simdutf::simdutf
            spdlog::spdlog
            sentbreak::sentbreak
            CURL::libcurl
            Qt6::Core
            Qt6::Network
            GTest::gtest
            GTest::gtest_main
    )
    target_include_directories(${target}
        PRIVATE
            ${QTRANS_APP_INCLUDE_DIRS}
            ${QTRANS_RUNTIME_INCLUDE_DIRS}
    )
    target_compile_definitions(${target} PRIVATE QTRANS_BUILD_TESTS)
    qtrans_link_llama_openmp(${target})
    qtrans_apply_project_definitions(${target})
    qtrans_configure_test_warnings(${target})
endfunction()

add_executable(qtrans_tests_runtime
    runtime/test_core_api.cpp
    runtime/test_model_host.cpp
    runtime/test_sentence_splitter.cpp
    ${QTRANS_RUNTIME_SOURCES}
)
qtrans_configure_runtime_test(qtrans_tests_runtime)
gtest_discover_tests(qtrans_tests_runtime
    PROPERTIES
        LABELS "dir:runtime"
)

add_executable(qtrans_tests_runtime_internal
    runtime/test_local_runtime_lifecycle.cpp
    ${QTRANS_RUNTIME_SOURCES}
)
qtrans_configure_runtime_test(qtrans_tests_runtime_internal)
gtest_discover_tests(qtrans_tests_runtime_internal
    PROPERTIES
        LABELS "dir:runtime"
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

    add_executable(qtrans_tests_${test_dir}
        ${_sources}
        ${QTRANS_RUNTIME_SOURCES}
        ${QTRANS_APP_SUPPORT_SOURCES}
    )
    qtrans_configure_app_test(qtrans_tests_${test_dir})

    gtest_discover_tests(qtrans_tests_${test_dir}
        PROPERTIES
            LABELS "dir:${test_dir}"
    )
endforeach()

# Test hook: services tests include model_host_test_access.h; production
# targets must not have the runtime internal include dirs. The shell's
# ModelFlow is compiled directly into the services tests because it is a UI
# source (it must not pull widget headers into the link).
if(TARGET qtrans_tests_services)
    target_include_directories(qtrans_tests_services PRIVATE ${QTRANS_RUNTIME_INTERNAL_INCLUDE_DIRS})
    target_sources(qtrans_tests_services PRIVATE
        ${QTRANS_SRC_DIR}/ui/shell/model_flow.cpp
        ${QTRANS_SRC_DIR}/ui/shell/model_flow.h
    )
endif()

# The batch queue table model lives in the executable sources, so its unit
# tests compile that model directly against the widget-free Qt modules.
add_executable(qtrans_tests_batchmodel
    batchmodel/test_batch_queue_model.cpp
    ${QTRANS_SRC_DIR}/ui/pages/batch/batch_queue_model.cpp
    ${QTRANS_SRC_DIR}/ui/pages/batch/batch_queue_model.h
)
target_link_libraries(qtrans_tests_batchmodel
    PRIVATE
        Qt6::Core
        Qt6::Gui
        GTest::gtest
        GTest::gtest_main
)
target_include_directories(qtrans_tests_batchmodel PRIVATE ${QTRANS_APP_INCLUDE_DIRS})
qtrans_configure_test_warnings(qtrans_tests_batchmodel)
gtest_discover_tests(qtrans_tests_batchmodel
    PROPERTIES
        LABELS "dir:batchmodel"
)

add_library(qtrans_core STATIC
    src/backend_environment.cpp
    src/translation_model.cpp
    src/translator.cpp
    src/runtime/local_runtime.cpp
    src/runtime/remote_runtime.cpp
    src/runtime/default_runtime_factory.cpp
    src/text/utf8.cpp
    src/text/utf8_stream_buffer.cpp
    src/text/sentence_splitter.cpp
    src/text/chunker.cpp
)

target_include_directories(qtrans_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(qtrans_core
    PRIVATE
        llama
        CURL::libcurl
        ICU::uc
        ICU::i18n
        ICU::data
        simdutf::simdutf
        spdlog::spdlog
)

target_compile_definitions(qtrans_core
    PRIVATE
        $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_WARN>
)

if(QTRANS_MULTI_BACKEND_VALUE)
    target_compile_definitions(qtrans_core PRIVATE QTRANS_MULTI_BACKEND)
endif()

if(QTRANS_GPU_METAL_VALUE)
    target_compile_definitions(qtrans_core PRIVATE QTRANS_GPU_METAL)
endif()

if(QTRANS_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)
    add_executable(qtrans_tests_core
        tests/test_core_api.cpp
        tests/test_translation_model.cpp
    )
    target_link_libraries(qtrans_tests_core
        PRIVATE
            qtrans_core
            GTest::gtest
            GTest::gtest_main
    )
    target_compile_options(qtrans_tests_core PRIVATE -Wall -Wextra -Wpedantic)
    gtest_discover_tests(qtrans_tests_core
        PROPERTIES
            LABELS "dir:core"
    )
endif()

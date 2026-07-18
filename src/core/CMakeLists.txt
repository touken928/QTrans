add_library(qtrans_core STATIC
    diagnostics.cpp
    backend_environment.cpp
    translation_model.cpp
    translator.cpp
    runtime/local_runtime.cpp
    runtime/remote_runtime.cpp
    runtime/default_runtime_factory.cpp
    text/utf8.cpp
    text/utf8_stream_buffer.cpp
    text/sentence_splitter.cpp
    text/chunker.cpp
)

target_include_directories(qtrans_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
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

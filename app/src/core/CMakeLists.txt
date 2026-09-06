add_library(qtrans_core STATIC
    internal/diagnostics.cpp
    internal/backend.cpp
    internal/local_runtime.cpp
    internal/host/model_host.cpp
    internal/host/model_runtime.cpp
    internal/host/prompt_profiles.cpp
    internal/host/invocation_scheduler.cpp
    internal/text/utf8.cpp
    internal/text/utf8_stream_buffer.cpp
    internal/text/sentence_splitter.cpp
    internal/text/chunker.cpp
)

target_sources(qtrans_core
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/qtrans/core.h
)

target_include_directories(qtrans_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/internal
)

target_link_libraries(qtrans_core
    PUBLIC
        llama-cpp::llama
        simdutf::simdutf
        spdlog::spdlog
        ICU::uc
        ICU::i18n
        ICU::data
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

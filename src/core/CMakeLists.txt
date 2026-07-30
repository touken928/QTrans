include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

add_library(qtrans_core STATIC
    internal/translator.cpp
    internal/diagnostics.cpp
    internal/backend.cpp
    internal/local_runtime.cpp
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
        llama
        ICU::uc
        ICU::i18n
        ICU::data
        simdutf::simdutf
        spdlog::spdlog
)

set_target_properties(qtrans_core PROPERTIES EXPORT_NAME Core)

install(
    TARGETS qtrans_core
    EXPORT QTransCoreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT QTransCoreTargets
    FILE QTransCoreTargets.cmake
    NAMESPACE QTrans::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QTransCore
)

configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/QTransCoreConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/QTransCoreConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QTransCore
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/QTransCoreConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/QTransCoreConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/QTransCoreConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QTransCore
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

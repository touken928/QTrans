cmake_minimum_required(VERSION 3.31)

project(QTrans VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$" OR
       NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "QTrans supports macOS ARM64 with Clang only")
    endif()
elseif(WIN32)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
       NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR
       NOT CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC" OR
       NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "QTrans supports Windows x64 with clang-cl and the MSVC ABI only")
    endif()
    if(NOT CMAKE_LINKER_TYPE STREQUAL "LLD")
        message(FATAL_ERROR "QTrans Windows builds require lld-link (CMAKE_LINKER_TYPE=LLD)")
    endif()
else()
    message(FATAL_ERROR "QTrans supports macOS ARM64 with Clang and Windows x64 with clang-cl only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(llama-cpp CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(sentbreak CONFIG REQUIRED)
find_package(simdutf CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(QTRANS_MULTI_BACKEND_VALUE 1)
else()
    set(QTRANS_MULTI_BACKEND_VALUE 0)
endif()

if(APPLE)
    set(QTRANS_GPU_METAL_VALUE 1)
else()
    set(QTRANS_GPU_METAL_VALUE 0)
endif()

option(QTRANS_BUILD_TESTS "Build unit tests" OFF)

if(QTRANS_BUILD_TESTS)
    enable_testing()
endif()

include(src/sources.cmake)

add_executable(QTrans
    src/main.cpp
    ${QTRANS_RUNTIME_SOURCES}
    ${QTRANS_APP_SUPPORT_SOURCES}
    ${QTRANS_UI_SOURCES}
    ${QTRANS_PLATFORM_SOURCES}
)

if(WIN32)
    target_sources(QTrans PRIVATE ${QTRANS_PLATFORM_SOURCES_WIN})
    set_target_properties(QTrans PROPERTIES WIN32_EXECUTABLE TRUE)
    set(_QTRANS_PLUGIN_TARGET Qt6::QWindowsIntegrationPlugin)
    set(_QTRANS_PLUGIN_CLASS QWindowsIntegrationPlugin)
elseif(APPLE)
    target_sources(QTrans PRIVATE ${QTRANS_PLATFORM_SOURCES_MAC})
    target_link_libraries(QTrans PRIVATE
        "-framework AppKit"
        "-framework ApplicationServices"
        "-framework Carbon"
        "-framework CoreGraphics"
        "-framework IOSurface"
    )
    set(_QTRANS_PLUGIN_TARGET Qt6::QCocoaIntegrationPlugin)
    set(_QTRANS_PLUGIN_CLASS QCocoaIntegrationPlugin)
endif()

# The Qt platform plugin must be linked into the executable because every Qt
# dependency in this project is static. Conan's CMakeDeps Qt package exposes
# the plugin as an INTERFACE target, but without the QT_PLUGIN_* properties
# that Qt's official qt_import_plugins() requires, so that call is a no-op and
# the plugin would be missing at runtime ("Could not find the Qt platform
# plugin ..."). Link the plugin target directly and import it via
# Q_IMPORT_PLUGIN instead.
if(DEFINED _QTRANS_PLUGIN_TARGET)
    target_compile_definitions(QTrans PRIVATE QT_STATICPLUGIN)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/qt_plugin_import_QTrans.cpp"
        "#include <QtPlugin>\n"
        "Q_IMPORT_PLUGIN(${_QTRANS_PLUGIN_CLASS})\n"
    )
    target_sources(QTrans PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/qt_plugin_import_QTrans.cpp")
    # Link the plugin ahead of the Qt targets so its Qt symbol references
    # resolve against the Qt libraries that follow it on the link line.
    target_link_libraries(QTrans PRIVATE ${_QTRANS_PLUGIN_TARGET})
endif()

target_include_directories(QTrans PRIVATE
    ${QTRANS_APP_INCLUDE_DIRS}
    ${QTRANS_RUNTIME_INCLUDE_DIRS}
)

target_link_libraries(QTrans PRIVATE
    llama-cpp::llama
    simdutf::simdutf
    spdlog::spdlog
    sentbreak::sentbreak
    CURL::libcurl
    Qt6::Widgets
    Qt6::Network
)

qtrans_apply_project_definitions(QTrans)

if(QTRANS_BUILD_TESTS)
    target_compile_definitions(QTrans PRIVATE QTRANS_BUILD_TESTS)
    add_subdirectory(tests)
endif()

cmake_minimum_required(VERSION 3.31)

project(QTrans VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$" OR
       NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "QTrans supports macOS ARM64 with Clang only")
    endif()
elseif(WIN32)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "QTrans supports Windows x64 with MSVC only")
    endif()
else()
    message(FATAL_ERROR "QTrans supports macOS ARM64 with Clang and Windows x64 with MSVC only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(llama CONFIG REQUIRED)
find_package(CURL REQUIRED)
if(WIN32)
    # The vcpkg ICU package currently exposes an import library on Windows;
    # keep the release executable self-contained and use the built-in splitter.
    set(QTRANS_USE_ICU 0)
else()
    find_package(ICU REQUIRED COMPONENTS uc i18n data)
    set(QTRANS_USE_ICU 1)
endif()

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

if(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "osx")
    set(QTRANS_GPU_METAL_VALUE 1)
else()
    set(QTRANS_GPU_METAL_VALUE 0)
endif()

option(QTRANS_BUILD_TESTS "Build unit tests" OFF)

if(QTRANS_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src/core)
add_subdirectory(src/desktop)

if(QTRANS_BUILD_TESTS)
    add_subdirectory(tests)
endif()

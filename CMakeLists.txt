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
find_package(ICU REQUIRED COMPONENTS uc i18n data)

# CMake's FindICU prefers the unprefixed Windows library names.  Some vcpkg
# ICU builds install both the import library (icuuc.lib) and the static archive
# (sicuuc.lib), so explicitly select the static archive for our static triplet.
# This also applies to Qt's transitive ICU targets and keeps the final app
# independent of icu*.dll.
if(MSVC AND VCPKG_TARGET_TRIPLET MATCHES "static")
    set(_qtrans_icu_lib_dir
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
    foreach(_qtrans_icu_component IN ITEMS uc i18n data)
        if(_qtrans_icu_component STREQUAL "uc")
            set(_qtrans_icu_static_names sicuuc icuuc)
        elseif(_qtrans_icu_component STREQUAL "i18n")
            set(_qtrans_icu_static_names sicuin sicui18n icuin icui18n)
        else()
            set(_qtrans_icu_static_names sicudt icudt)
        endif()
        unset(_qtrans_icu_static_library)
        find_library(_qtrans_icu_static_library
            NAMES ${_qtrans_icu_static_names}
            PATHS "${_qtrans_icu_lib_dir}"
            NO_DEFAULT_PATH)
        if(NOT _qtrans_icu_static_library)
            message(FATAL_ERROR
                "Static ICU library for ${_qtrans_icu_component} was not found in ${_qtrans_icu_lib_dir}")
        endif()
        set_target_properties(ICU::${_qtrans_icu_component} PROPERTIES
            IMPORTED_LOCATION "${_qtrans_icu_static_library}"
            IMPORTED_LOCATION_RELEASE "${_qtrans_icu_static_library}")
        unset(_qtrans_icu_static_library CACHE)
    endforeach()
    unset(_qtrans_icu_lib_dir)
    unset(_qtrans_icu_static_names)
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

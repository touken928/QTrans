cmake_minimum_required(VERSION 3.20)

project(sentbreak VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(sentbreak src/segmenter.cpp)
add_library(sentbreak::sentbreak ALIAS sentbreak)

target_compile_features(sentbreak PUBLIC cxx_std_17)
target_include_directories(sentbreak
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

install(TARGETS sentbreak
    EXPORT sentbreakTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT sentbreakTargets
    FILE sentbreakTargets.cmake
    NAMESPACE sentbreak::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sentbreak
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/sentbreakConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sentbreakConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/sentbreakConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sentbreak
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/sentbreakConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/sentbreakConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sentbreak
)

option(SENTBREAK_BUILD_TESTS "Build sentbreak tests" OFF)
if(SENTBREAK_BUILD_TESTS)
    enable_testing()
    add_executable(sentbreak_tests tests/segmenter_test.cpp)
    target_link_libraries(sentbreak_tests PRIVATE sentbreak)
    add_test(NAME sentbreak_tests COMMAND sentbreak_tests)
endif()

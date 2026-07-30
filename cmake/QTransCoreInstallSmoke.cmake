if(NOT DEFINED QTRANS_BUILD_DIR OR NOT DEFINED QTRANS_SOURCE_DIR)
    message(FATAL_ERROR "QTRANS_BUILD_DIR and QTRANS_SOURCE_DIR are required")
endif()

set(smoke_root "${CMAKE_CURRENT_BINARY_DIR}/qtrans-core-install-smoke")
file(REMOVE_RECURSE "${smoke_root}")
file(MAKE_DIRECTORY "${smoke_root}/consumer")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${QTRANS_BUILD_DIR}" --prefix "${smoke_root}/install"
    RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "core install failed: ${install_result}")
endif()

file(WRITE "${smoke_root}/consumer/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.21)
project(QTransCoreConsumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(QTransCore CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE QTrans::Core)
]=])
file(WRITE "${smoke_root}/consumer/main.cpp" [=[
#include <qtrans/core.h>

int main() {
    qtrans::core::Translator translator;
    return translator.loaded() ? 1 : 0;
}
]=])

set(prefix_path "${smoke_root}/install")
if(DEFINED QTRANS_DEPENDENCY_PREFIX AND NOT QTRANS_DEPENDENCY_PREFIX STREQUAL "")
    list(APPEND prefix_path "${QTRANS_DEPENDENCY_PREFIX}")
endif()
list(REMOVE_DUPLICATES prefix_path)
string(JOIN ";" prefix_path_argument ${prefix_path})

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${smoke_root}/consumer" -B "${smoke_root}/build"
            "-DCMAKE_PREFIX_PATH=${prefix_path_argument}"
    RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "core consumer configure failed: ${configure_result}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${smoke_root}/build"
    RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "core consumer build failed: ${build_result}")
endif()

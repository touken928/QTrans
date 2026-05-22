set(VCPKG_POLICY_SKIP_MISPLACED_CMAKE_FILES_CHECK enabled)
set(VCPKG_POLICY_SKIP_LIB_CMAKE_MERGE_CHECK enabled)
set(VCPKG_POLICY_SKIP_ABSOLUTE_PATHS_CHECK enabled)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ggml-org/llama.cpp
    REF 45c4c1c618b74b9911e5cbe5910ad0caba085d2d
    SHA512 ff295c33cd23ee888daa2100bb2b5c3373bd495488bfb1a2851c06b4a8d14a987bc7aa75496f3f74008ff04b755f46583c347ac1e68ae35ccd0eee53607b932d
    HEAD_REF pull/22836/head
)

# vcpkg_cmake_config_fixup removes bundled ggml cmake files; patch llama-config and restore ggml-config after install.
set(LLAMA_CONFIG "${SOURCE_PATH}/cmake/llama-config.cmake.in")
file(READ "${LLAMA_CONFIG}" LLAMA_CONFIG_CONTENTS)
string(REPLACE
    "find_package(ggml REQUIRED HINTS \${LLAMA_LIB_DIR}/cmake)"
    "find_package(ggml REQUIRED CONFIG HINTS \${LLAMA_LIB_DIR}/cmake)"
    LLAMA_CONFIG_CONTENTS "${LLAMA_CONFIG_CONTENTS}")
string(REPLACE
    "add_library(llama UNKNOWN IMPORTED)"
    "if(NOT TARGET llama)\nadd_library(llama UNKNOWN IMPORTED)"
    LLAMA_CONFIG_CONTENTS "${LLAMA_CONFIG_CONTENTS}")
string(REPLACE
    "        POSITION_INDEPENDENT_CODE ON)\n\ncheck_required_components(Llama)"
    "        POSITION_INDEPENDENT_CODE ON)\nendif()\n\ncheck_required_components(Llama)"
    LLAMA_CONFIG_CONTENTS "${LLAMA_CONFIG_CONTENTS}")
file(WRITE "${LLAMA_CONFIG}" "${LLAMA_CONFIG_CONTENTS}")

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" BUILD_SHARED_LIBS)

# MinGW static releases should not depend on libgomp-1.dll at runtime.
if(VCPKG_TARGET_TRIPLET MATCHES "mingw")
    set(GGML_OPENMP OFF)
else()
    set(GGML_OPENMP ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
        -DGGML_OPENMP=${GGML_OPENMP}
        -DGGML_CCACHE=OFF
        -DGGML_VULKAN=OFF
        -DGGML_METAL=OFF
        -DGGML_NATIVE=OFF
        -DLLAMA_USE_SYSTEM_GGML=OFF
        -DLLAMA_ALL_WARNINGS=OFF
        -DLLAMA_BUILD_TESTS=OFF
        -DLLAMA_BUILD_EXAMPLES=OFF
        -DLLAMA_BUILD_TOOLS=OFF
        -DLLAMA_BUILD_SERVER=OFF
        -DLLAMA_BUILD_COMMON=OFF
        -DVCPKG_LOCK_FIND_PACKAGE_Git=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/llama")

file(COPY
    "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/ggml/ggml-config.cmake"
    "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/ggml/ggml-version.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib/cmake/ggml"
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_clean_executables_in_bin(FILE_NAMES none)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

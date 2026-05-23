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
    # Do not pass -DGGML_OPENMP=${GGML_OPENMP}: CMake's OFF expands to an empty string.
    set(GGML_OPENMP_OPTIONS
        -DGGML_OPENMP:BOOL=OFF
        -DGGML_OPENMP_ENABLED:STRING=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_OpenMP=ON
        -DOpenMP_C_FOUND=FALSE
        -DOpenMP_CXX_FOUND=FALSE
        -DCMAKE_C_FLAGS=-fno-openmp
        -DCMAKE_CXX_FLAGS=-fno-openmp
    )

    set(GGML_CMAKE "${SOURCE_PATH}/ggml/CMakeLists.txt")
    if(EXISTS "${GGML_CMAKE}")
        file(READ "${GGML_CMAKE}" GGML_CMAKE_CONTENTS)
        string(REPLACE
            "option(GGML_OPENMP \"ggml: use OpenMP\" ON)"
            "option(GGML_OPENMP \"ggml: use OpenMP\" OFF)"
            GGML_CMAKE_CONTENTS "${GGML_CMAKE_CONTENTS}")
        file(WRITE "${GGML_CMAKE}" "${GGML_CMAKE_CONTENTS}")
    endif()

    set(GGML_CPU_CMAKE "${SOURCE_PATH}/ggml/src/ggml-cpu/CMakeLists.txt")
    if(EXISTS "${GGML_CPU_CMAKE}")
        file(READ "${GGML_CPU_CMAKE}" GGML_CPU_CMAKE_CONTENTS)
        string(REGEX REPLACE
            " if \\(GGML_OPENMP\\)\r?\n find_package\\(OpenMP\\)"
            " if (FALSE) # GGML_OPENMP disabled for MinGW static builds\n find_package(OpenMP)"
            GGML_CPU_CMAKE_CONTENTS "${GGML_CPU_CMAKE_CONTENTS}")
        file(WRITE "${GGML_CPU_CMAKE}" "${GGML_CPU_CMAKE_CONTENTS}")
    endif()

    # ggml-config.cmake.in always contains an OpenMP block guarded by
    # GGML_OPENMP_ENABLED; drop it so consumers never see find_dependency(OpenMP).
    set(GGML_CONFIG_IN "${SOURCE_PATH}/ggml/cmake/ggml-config.cmake.in")
    if(EXISTS "${GGML_CONFIG_IN}")
        file(READ "${GGML_CONFIG_IN}" GGML_CONFIG_IN_CONTENTS)
        string(REGEX REPLACE
            "[ \t]*if \\(GGML_OPENMP_ENABLED\\)\r?\n[ \t]*find_dependency\\(OpenMP\\)\r?\n[ \t]*list\\(APPEND GGML_CPU_INTERFACE_LINK_LIBRARIES OpenMP::OpenMP_C OpenMP::OpenMP_CXX\\)\r?\n[ \t]*endif\\(\\)\r?\n?"
            ""
            GGML_CONFIG_IN_CONTENTS "${GGML_CONFIG_IN_CONTENTS}")
        file(WRITE "${GGML_CONFIG_IN}" "${GGML_CONFIG_IN_CONTENTS}")
    endif()
else()
    set(GGML_OPENMP_OPTIONS -DGGML_OPENMP:BOOL=ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
        ${GGML_OPENMP_OPTIONS}
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

if(VCPKG_TARGET_TRIPLET MATCHES "mingw")
    foreach(CONFIG_FILE
        "${CURRENT_PACKAGES_DIR}/lib/cmake/ggml/ggml-config.cmake"
        "${CURRENT_PACKAGES_DIR}/lib/cmake/llama/llama-config.cmake"
    )
        if(EXISTS "${CONFIG_FILE}")
            file(READ "${CONFIG_FILE}" CONFIG_CONTENTS)
            string(REPLACE "\r\n" "\n" CONFIG_CONTENTS "${CONFIG_CONTENTS}")
            string(REGEX REPLACE
                "[ \t]*if \\(GGML_OPENMP_ENABLED\\)\n[ \t]*find_dependency\\(OpenMP\\)\n[ \t]*list\\(APPEND GGML_CPU_INTERFACE_LINK_LIBRARIES OpenMP::OpenMP_C OpenMP::OpenMP_CXX\\)\n[ \t]*endif\\(\\)\n?"
                ""
                CONFIG_CONTENTS "${CONFIG_CONTENTS}")
            string(REGEX REPLACE "find_dependency\\(OpenMP\\)\n?" "" CONFIG_CONTENTS "${CONFIG_CONTENTS}")
            string(REGEX REPLACE "[^\n]*OpenMP::OpenMP_[^\n]*\n?" "" CONFIG_CONTENTS "${CONFIG_CONTENTS}")
            string(REPLACE "set(GGML_OPENMP \"ON\")" "set(GGML_OPENMP \"OFF\")" CONFIG_CONTENTS "${CONFIG_CONTENTS}")
            string(REPLACE "set(GGML_OPENMP_ENABLED \"ON\")" "set(GGML_OPENMP_ENABLED \"OFF\")" CONFIG_CONTENTS "${CONFIG_CONTENTS}")
            file(WRITE "${CONFIG_FILE}" "${CONFIG_CONTENTS}")
        endif()
    endforeach()

    set(GGML_CONFIG "${CURRENT_PACKAGES_DIR}/lib/cmake/ggml/ggml-config.cmake")
    if(EXISTS "${GGML_CONFIG}")
        file(READ "${GGML_CONFIG}" GGML_CONFIG_CONTENTS)
        if(GGML_CONFIG_CONTENTS MATCHES "GGML_OPENMP_ENABLED \"ON\"|GGML_OPENMP \"ON\"|find_dependency\\(OpenMP\\)")
            message(FATAL_ERROR "ggml-config.cmake still enables OpenMP for MinGW builds")
        endif()
    endif()
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_clean_executables_in_bin(FILE_NAMES none)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

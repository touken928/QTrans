set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# Qt enables the Windows SDK ICU backend by autodetection even when its
# optional ICU feature is disabled. Keep the release executable self-contained.
set(VCPKG_CMAKE_CONFIGURE_OPTIONS_RELEASE
    "-DFEATURE_winsdkicu:BOOL=OFF")

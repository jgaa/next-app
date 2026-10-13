set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# Qt defaults to C++17 independently of the application's CXX_STANDARD.
# Its Windows/WinRT objects must use the same coroutine ABI as our C++20 app.
# Qt records this feature in qtbase and propagates it to its other modules.
if(PORT STREQUAL "qtbase")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS "-DFEATURE_cxx20=ON")
endif()

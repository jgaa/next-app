cmake_minimum_required(VERSION 3.30)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM windows+pe)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL dumpbin)
find_program(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND dumpbin REQUIRED)
file(GLOB_RECURSE deployed_dlls "${STAGE_DIR}/bin/*.dll")
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${STAGE_DIR}/bin/nextapp.exe"
    LIBRARIES ${deployed_dlls}
    DIRECTORIES "${STAGE_DIR}/bin" "${BUILD_DIR}/bin" "${DEPENDENCY_BIN}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    PRE_EXCLUDE_REGEXES "[Aa][Pp][Ii]-[Mm][Ss]-.*" "[Ee][Xx][Tt]-[Mm][Ss]-.*"
    # CMake may return mixed separators, e.g. C:\WINDOWS\system32/msvcp140.dll.
    # Exclude system copies before conflict detection; keep our staged CRT DLLs.
    POST_EXCLUDE_REGEXES [[.*[\/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\/\\][Ss][Yy][Ss][Tt][Ee][Mm]32[\/\\].*]])
if(unresolved)
    message(FATAL_ERROR "Missing runtime dependencies: ${unresolved}")
endif()
foreach(dll IN LISTS resolved)
    file(COPY "${dll}" DESTINATION "${STAGE_DIR}/bin")
endforeach()

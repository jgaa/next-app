set(SCRIPT_PATH "${CURRENT_INSTALLED_DIR}/share/qtbase")
include("${SCRIPT_PATH}/qt_install_submodule.cmake")

set(${PORT}_PATCHES protoc-host.patch)
set(TOOL_NAMES qtprotobufgen qtgrpcgen)
# native_grpc ->grpc WrapgRPC
# grp -> qt[network]
# qtprotobufgen -> protobuf WrapProtobuf
qt_install_submodule(PATCHES    ${${PORT}_PATCHES}
                     TOOL_NAMES ${TOOL_NAMES}
                     CONFIGURE_OPTIONS
                        -DCMAKE_FIND_PACKAGE_TARGETS_GLOBAL=ON
                        # These are required by NextApp. Autodetection can
                        # silently omit generated modules when a tool's link
                        # test fails, producing an unusable cached package.
                        -DFEATURE_qtprotobufgen=ON
                        -DFEATURE_qtgrpcgen=ON
                        -DFEATURE_grpc=ON
                        -DFEATURE_protobuf_qtcoretypes=ON
                        -DFEATURE_protobuf_wellknowntypes=ON
                        -DFEATURE_protobufquick=ON
                     CONFIGURE_OPTIONS_MAYBE_UNUSED
                         QT_BUILD_EXAMPLES
                         QT_USE_DEFAULT_CMAKE_OPTIMIZATION_FLAGS
                    )

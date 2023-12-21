cmake_minimum_required(VERSION 3.16)

project(NextAppUi VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Quick Protobuf Grpc)

qt_standard_project_setup()

set (protofile "${CMAKE_CURRENT_SOURCE_DIR}/../proto/nextapp.proto")

qt_add_protobuf(MyProtoMessageLib
    PROTO_FILES ${protofile}
)

qt_add_grpc(MyGrpcClient CLIENT
    PROTO_FILES ${protofile}
)

qt_add_executable(appNextAppUi
    main.cpp
)

qt_add_qml_module(appNextAppUi
    URI NextAppUi
    VERSION 1.0
    QML_FILES Main.qml
    SOURCES ServerComm.h ServerComm.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appNextAppUi PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appNextAppUi
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appNextAppUi
    PRIVATE Qt6::Quick MyGrpcClient MyProtoMessageLib Qt6::Protobuf Qt6::Grpc
)

include(GNUInstallDirs)
install(TARGETS appNextAppUi
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

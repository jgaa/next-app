cmake_minimum_required(VERSION 3.16)

project(NextAppUi VERSION 0.1 LANGUAGES CXX)
add_definitions(-DNEXTAPP_VERSION=\"${CMAKE_PROJECT_VERSION}\")

set(NEXTAPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(WITH_TESTS "Enable Tests" ON)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Svg Test Protobuf Grpc)

qt_policy(
    SET QTP0001 NEW
)

include(cmake/3rdparty.cmake)

qt_standard_project_setup()

set (protofile "${CMAKE_CURRENT_SOURCE_DIR}/../proto/nextapp.proto")

qt_add_protobuf(MyProtoMessageLib
    QML
    QML_URI nextapp.pb
    PROTO_FILES ${protofile}
)

qt_add_grpc(MyGrpcClient CLIENT
    PROTO_FILES ${protofile}
)

qt_add_executable(appNextAppUi
    main.cpp
    logging.h
)

target_include_directories(appNextAppUi PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include>
    PRIVATE src
)

set_source_files_properties(qml/Colors.qml
    PROPERTIES
        QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(appNextAppUi
    URI NextAppUi
    VERSION ${VERSION}
    QML_FILES
        Main.qml
        qml/MainTree.qml
        qml/Colors.qml # x
        qml/MyMenuBar.qml # x
        qml/MyMenu.qml # x
        qml/WindowDragHandler.qml # x
        qml/Sidebar.qml
        qml/About.qml
        qml/ResizeButton.qml # x
        qml/DaysInYear.qml
    RESOURCES
        icons/folder_closed.svg
        icons/folder_open.svg
        icons/generic_file.svg
        icons/globe.svg
        icons/info_sign.svg
        icons/light_bulb.svg
        icons/read.svg
        icons/resize.svg
        icons/nextapp.svg
        icons/fontawsome/calendar-days.svg
    SOURCES
        ServerComm.h
        ServerComm.cpp
        MainTreeModel.h
        MainTreeModel.cpp
        NextAppCore.h
        NextAppCore.cpp
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

add_dependencies(appNextAppUi logfault)

target_link_libraries(appNextAppUi
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Svg
        Qt6::Protobuf
        Qt6::Grpc
        MyGrpcClient
        MyProtoMessageLib
)

include(GNUInstallDirs)
install(TARGETS appNextAppUi
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if (WITH_TESTS)
    find_package(GTest REQUIRED)

    enable_testing()

    qt_add_executable(tst_data_models
        tst_data_models.cpp
        MainTreeModel.h MainTreeModel.cpp
    )

    # add_dependencies(tst_data_models
    #     ${DEPENDS_GTEST}
    # )

    set_property(TARGET tst_data_models PROPERTY CXX_STANDARD 20)

    target_link_libraries(tst_data_models
        PRIVATE MyGrpcClient MyProtoMessageLib Qt6::Protobuf Qt6::Grpc Qt6::Core ${GTEST_LIBRARIES}
    )

    add_test(NAME data_models COMMAND tst_data_models)

endif() # tests


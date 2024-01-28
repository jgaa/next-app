cmake_minimum_required(VERSION 3.16)

project(NextAppUi VERSION 0.2 LANGUAGES CXX)
add_definitions(-DNEXTAPP_VERSION=\"${CMAKE_PROJECT_VERSION}\")

set(NEXTAPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(SERVER_ADDRESS "http://127.0.0.1:10321" CACHE STRING "Default address to server")
add_definitions("-DSERVER_ADDRESS=\"${SERVER_ADDRESS}\"")

option(WITH_TREE_MODEL_TESTING "Enable ongoing tests for the MainTreeModel" OFF)
if (WITH_TREE_MODEL_TESTING)
    add_definitions(-DWITH_TREE_MODEL_TESTING)
endif()

option(WITH_TESTS "Enable Tests" ON)
option(DEVEL_SETTINGS "Use '-devel' postfix for the settings location." ON)
if (DEVEL_SETTINGS)
    add_definitions(-DDEVEL_SETTINGS)
endif()

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Svg Test Protobuf Grpc Concurrent Test)

qt_policy(
    SET QTP0001 NEW
)

include(cmake/3rdparty.cmake)

qt_standard_project_setup()

set (protofile "${CMAKE_CURRENT_SOURCE_DIR}/../proto/nextapp.proto")

qt_add_protobuf(NextappProtoMessageLib
    QML
    QML_URI nextapp.pb
    PROTO_FILES ${protofile}
)

qt_add_grpc(NextappGrpcClient CLIENT
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
        qml/DayDialog.qml
        qml/SettingsDlg.qml
        qml/ServerSettings.qml
        qml/EditNodeDlg.qml
        qml/DlgInputField.qml
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
        icons/folder.svg
        icons/organization.svg
        icons/person.svg
        icons/project.svg
        icons/task.svg
        icons/fontawsome/calendar-days.svg
        icons/fontawsome/folder.svg
        icons/fontawsome/trash-can.svg
        icons/fontawsome/pen-to-square.svg
        icons/fontawsome/angle-down.svg
        icons/fontawsome/angle-right.svg
        icons/fontawsome/circle.svg
    SOURCES
        ServerComm.h
        ServerComm.cpp
        MainTreeModel.h
        MainTreeModel.cpp
        NextAppCore.h
        NextAppCore.cpp
        MonthModel.h
        MonthModel.cpp
        DayColorModel.h
        DayColorModel.cpp
        DaysModel.h
        DaysModel.cpp
        DayModel.h
        DayModel.cpp
        SOURCES util.h util.cpp
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
    RUNTIME_OUTPUT_NAME nextapp
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_dependencies(appNextAppUi logfault NextappGrpcClient NextappProtoMessageLib)

target_link_libraries(appNextAppUi
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Svg
        Qt6::Protobuf
        Qt6::Grpc
        Qt6::Concurrent
        Qt6::Test
        NextappGrpcClient
        NextappProtoMessageLib
)

include(GNUInstallDirs)
install(TARGETS appNextAppUi
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET appNextAppUi
    OUTPUT_SCRIPT deploy_script

)
install(SCRIPT ${deploy_script})

# if (WITH_TESTS)
#     find_package(GTest REQUIRED)

#     enable_testing()

#     qt_add_executable(tst_data_models
#         tst_data_models.cpp
#         MainTreeModel.h MainTreeModel.cpp
#     )

#     # add_dependencies(tst_data_models
#     #     ${DEPENDS_GTEST}
#     # )

#     set_property(TARGET tst_data_models PROPERTY CXX_STANDARD 20)

#     target_link_libraries(tst_data_models
#         PRIVATE NextappGrpcClient NextappProtoMessageLib Qt6::Protobuf Qt6::Grpc Qt6::Core ${GTEST_LIBRARIES}
#     )

#     add_test(NAME data_models COMMAND tst_data_models)

# endif() # tests

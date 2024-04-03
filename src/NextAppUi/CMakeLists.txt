cmake_minimum_required(VERSION 3.16)

project(NextAppUi VERSION 0.3 LANGUAGES CXX)
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

#include(cmake/3rdparty.cmake)
# Use logfault from git submodule in stead of adding it
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/logfault/include)
add_library(logfault INTERFACE)

qt_standard_project_setup()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-D_DEBUG)
endif()

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
        qml/About.qml
        qml/ActionsList.qml
        qml/Colors.qml # x
        qml/DayDialog.qml
        qml/DaysInYear.qml
        qml/DlgInputField.qml
        qml/DueSelectionDialog.qml
        qml/EditActionDlg.qml
        qml/EditNodeDlg.qml
        qml/MainTree.qml
        qml/MyMenu.qml # x
        qml/MyMenuBar.qml # x
        qml/ResizeButton.qml # x
        qml/ServerSettings.qml
        qml/SettingsDlg.qml
        qml/Sidebar.qml
        qml/StyledCheckBox.qml
        qml/WhenControl.qml
        qml/WindowDragHandler.qml # x
        qml/DatePicker.qml
        qml/common.js
        qml/CheckBoxWithFontIcon.qml
        qml/WorkSessions.qml
        qml/CommonElements.qml
    RESOURCES
        icons/folder.svg
        icons/folder_closed.svg
        icons/folder_open.svg
        icons/fontawsome/angle-down.svg
        icons/fontawsome/angle-right.svg
        icons/fontawsome/calendar-days.svg
        icons/fontawsome/calendar.svg
        icons/fontawsome/circle.svg
        icons/fontawsome/folder.svg
        icons/fontawsome/pen-to-square.svg
        icons/fontawsome/square-check.svg
        icons/fontawsome/square.svg
        icons/fontawsome/trash-can.svg
        icons/fontawsome/clock.svg
        icons/generic_file.svg
        icons/globe.svg
        icons/info_sign.svg
        icons/light_bulb.svg
        icons/nextapp.svg
        icons/organization.svg
        icons/person.svg
        icons/project.svg
        icons/read.svg
        icons/resize.svg
        icons/square-checked.svg
        icons/square-unchecked.svg
        icons/task.svg
        "fonts/Font Awesome 6 Free-Regular-400.otf"
        "fonts/Font Awesome 6 Free-Solid-900.otf"
        "fonts/Font Awesome 6 Brands-Regular-400.otf"
    SOURCES
        ActionsModel.cpp
        ActionsModel.h
        DayColorModel.cpp
        DayColorModel.h
        DayModel.cpp
        DayModel.h
        DaysModel.cpp
        DaysModel.h
        MainTreeModel.cpp
        MainTreeModel.h
        MonthModel.cpp
        MonthModel.h
        NextAppCore.cpp
        NextAppCore.h
        ServerComm.cpp
        ServerComm.h
        WorkSessionsModel.cpp
        WorkSessionsModel.h
        util.cpp
        util.h
        QML_FILES
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

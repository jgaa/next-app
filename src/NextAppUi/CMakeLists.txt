cmake_minimum_required(VERSION 3.16)

project(appNextAppUi VERSION 0.1.9 LANGUAGES CXX)
add_definitions(-DNEXTAPP_VERSION=\"${CMAKE_PROJECT_VERSION}\")

set(NEXTAPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(SERVER_ADDRESS "http://127.0.0.1:10321" CACHE STRING "Default address to server")
add_definitions("-DSERVER_ADDRESS=\"${SERVER_ADDRESS}\"")

option(NEXTAPP_WITH_TREE_MODEL_TESTING "Enable ongoing tests for the MainTreeModel" OFF)
if (NEXTAPP_WITH_TREE_MODEL_TESTING)
    add_definitions(-DWITH_TREE_MODEL_TESTING)
endif()

option(WITH_TESTS "Enable Tests" ON)
option(NEXTAPP_DEVEL_SETTINGS "Use '-devel' postfix for the settings location to use a different profile than normal." ON)
if (NEXTAPP_DEVEL_SETTINGS)
    add_definitions(-DDEVEL_SETTINGS)
endif()
option(NEXTAPP_USE_ANDROID_UI "Use Android UI. Enable to develop Android UI on the Desktop." OFF)
if (NEXTAPP_USE_ANDROID_UI)
    add_definitions(-DUSE_ANDROID_UI)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT6_IS_SHARED_LIBS_BUILD OFF)
# find_package(Boost REQUIRED)
# include_directories(${Boost_INCLUDE_DIRS})

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Svg Test Protobuf Grpc Concurrent Test)

if(Qt6Core_VERSION VERSION_LESS "6.8.0")
    message("Using an old QT version. 6.8 is required for the current grpc plugin")
else ()
    find_package(Qt6 6.8 REQUIRED COMPONENTS ProtobufQuick)
    set (PROTOBUF_QML_DEPENDENCY Qt::ProtobufQuick)
endif()

qt_policy(
    SET QTP0001 NEW
)

qt_standard_project_setup()

#include(cmake/3rdparty.cmake)
# Use logfault from git submodule in stead of adding it
if (NOT TARGET logfault)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/logfault/include)
    add_library(logfault INTERFACE)
endif()


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

target_link_libraries(NextappGrpcClient
    PRIVATE
        NextappProtoMessageLib
        Qt6::Core
        Qt6::Protobuf
        ${PROTOBUF_QML_DEPENDENCY}
        Qt6::Grpc
)

#set(ANDROID_MANIFEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/AndroidManifest.xml)


qt_add_executable(${PROJECT_NAME}
    main.cpp
    logging.h
)

if(ANDROID)
    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${ANDROID_PACKAGE_SOURCE_DIR}")
endif()


target_include_directories(${PROJECT_NAME} PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include>
    PRIVATE src
)

if (NOT ANDROID)
    find_package(Boost REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${Boost_INCLUDE_DIRS})
else()
    target_include_directories(${PROJECT_NAME} PRIVATE /opt/boost/boost_1_85_0)
endif()

set_source_files_properties(qml/Colors.qml
    PROPERTIES
        QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(${PROJECT_NAME}
    URI NextAppUi
    VERSION ${VERSION}
    QML_FILES
        Main.qml
        qml/About.qml
        qml/ActionsList.qml
        qml/ActionsListFilterDlg.qml
        qml/ActionsListView.qml
        qml/CheckBoxWithFontIcon.qml
        qml/Colors.qml # x
        qml/CommonElements.qml
        qml/CurrentWorkSessionsView.qml
        qml/DatePicker.qml
        qml/DayDialog.qml
        qml/DaysInYear.qml
        qml/DisabledDimmer.qml
        qml/DlgInputField.qml
        qml/DueSelectionDialog.qml
        qml/EditActionDlg.qml
        qml/EditNodeDlg.qml
        qml/EditWorkSession.qml
        qml/MainTree.qml
        qml/MyMenu.qml # x
        qml/MyMenuBar.qml # x
        qml/ReportsView.qml
        qml/ResizeButton.qml # x
        qml/SelectedIndicatorBar.qml
        qml/Sidebar.qml
        qml/StyledButton.qml
        qml/StyledCheckBox.qml
        qml/StyledComboBox.qml
        qml/StyledHeaderView.qml
        qml/StyledTabButton.qml
        qml/WeeklyHoursView.qml
        qml/WhenControl.qml
        qml/WindowDragHandler.qml # x
        qml/WorkSessionList.qml
        qml/WorkSessionsView.qml
        qml/android/DrawerContent.qml
        qml/android/TodoList.qml
        qml/android/WorkSessionsCompactList.qml
        qml/android/WorkSessionsStacked.qml
        qml/android/main.qml
        qml/calendar/CalendarView.qml
        qml/calendar/DayPlan.qml
        qml/calendar/EditTimeBlockDlg.qml
        qml/calendar/ExpandArea.qml
        qml/calendar/HoursBar.qml
        qml/calendar/TimeBlock.qml
        qml/categories/CategoriesMgr.qml
        qml/categories/CategoryComboBox.qml
        qml/categories/ColorPicker.qml
        qml/categories/EditCategoryDlg.qml
        qml/common.js
        qml/settings/AdvancedSettings.qml
        qml/settings/GlobalSettings.qml
        qml/settings/PrefSettings.qml
        qml/settings/ServerSettings.qml
        qml/settings/SettingsDlg.qml
        qml/SplitViewComponent.qml
    RESOURCES
        "fonts/Font Awesome 6 Brands-Regular-400.otf"
        "fonts/Font Awesome 6 Free-Regular-400.otf"
        "fonts/Font Awesome 6 Free-Solid-900.otf"
        icons/folder.svg
        icons/folder_closed.svg
        icons/folder_open.svg
        icons/fontawsome/angle-down.svg
        icons/fontawsome/angle-right.svg
        icons/fontawsome/bars.svg
        icons/fontawsome/calendar-day.svg
        icons/fontawsome/calendar-days.svg
        icons/fontawsome/calendar.svg
        icons/fontawsome/chart-line.svg
        icons/fontawsome/circle.svg
        icons/fontawsome/clock.svg
        icons/fontawsome/folder.svg
        icons/fontawsome/pen-to-square.svg
        icons/fontawsome/square-check.svg
        icons/fontawsome/square.svg
        icons/fontawsome/gear.svg
        icons/fontawsome/list-check.svg
        icons/fontawsome/folder-tree.svg
        icons/fontawsome/trash-can.svg
        icons/fontawsome/cloud-bolt.svg
        icons/fontawsome/hourglass-half.svg
        icons/fontawsome/arrows-up-down-left-right.svg
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
    SOURCES
        ActionCategoriesModel.cpp
        ActionCategoriesModel.h
        ActionInfoCache.cpp
        ActionInfoCache.h
        ActionsModel.cpp
        ActionsModel.h
        CalendarDayModel.cpp
        CalendarDayModel.h
        CalendarModel.cpp
        CalendarModel.h
        DayColorModel.cpp
        DayColorModel.h
        DayModel.cpp
        DayModel.h
        DaysModel.cpp
        DaysModel.h
        DualView.h DualView.cpp
        MainTreeModel.cpp
        MainTreeModel.h
        MaterialDesignStyling.cpp
        MaterialDesignStyling.h
        MonthModel.cpp
        MonthModel.h
        NextAppCore.cpp
        NextAppCore.h
        ServerComm.cpp
        ServerComm.h
        TimeBoxActionsModel.cpp
        TimeBoxActionsModel.h
        WeeklyWorkReportModel.cpp
        WeeklyWorkReportModel.h
        WorkModel.cpp
        WorkModel.h
        WorkSessionsModel.cpp
        WorkSessionsModel.h
        util.cpp
        util.h
)

if(NOT ANDROID)
    # Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
    # If you are developing for iOS or macOS you should consider setting an
    # explicit, fixed bundle identifier manually though.
    set_target_properties(${PROJECT_NAME} PROPERTIES
    #    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.${PROJECT_NAME}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        MACOSX_BUNDLE TRUE
        WIN32_EXECUTABLE TRUE
        RUNTIME_OUTPUT_NAME nextapp
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
endif()

add_dependencies(${PROJECT_NAME} logfault NextappGrpcClient NextappProtoMessageLib)

target_link_libraries(${PROJECT_NAME}
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

if(NOT ANDROID)
    # include(GNUInstallDirs)
    # install(TARGETS ${PROJECT_NAME}
    #     BUNDLE DESTINATION .
    #     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    #     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    # )

    # qt_generate_deploy_app_script(
    #     TARGET ${PROJECT_NAME}
    #     OUTPUT_SCRIPT deploy_script

    # )
    # install(SCRIPT ${deploy_script})
    include(InstallRequiredSystemLibraries)
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGE_NAME "nextapp")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_CONTACT "jgaa@jgaa.com")

    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
    install(DIRECTORY ${CMAKE_PREFIX_PATH}/lib DESTINATION lib)

    include(CPack)

endif()

if (ANDROID)
    message(STATUS "Compiling for Android")
    target_compile_definitions(${PROJECT_NAME} PUBLIC __ANDROID__)
    include(GNUInstallDirs)
    install(TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION .
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

endif()

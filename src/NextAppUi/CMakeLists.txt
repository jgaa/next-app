cmake_minimum_required(VERSION 3.16)

project(appNextAppUi
    VERSION ${NEXTAPP_VERSION}
    LANGUAGES CXX)

add_definitions(-DNEXTAPP_UI_VERSION=\"${appNextAppUi_VERSION}\")

#set(NEXTAPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(SERVER_ADDRESS "https://signup.prod.next-app.org" CACHE STRING "Default address to signup server")
add_definitions("-DSERVER_ADDRESS=\"${SERVER_ADDRESS}\"")

option(NEXTAPP_WITH_TREE_MODEL_TESTING "Enable ongoing tests for the MainTreeModel" OFF)
if (NEXTAPP_WITH_TREE_MODEL_TESTING)
    add_definitions(-DWITH_TREE_MODEL_TESTING)
endif()

option(USE_STATIC_QT "Build with static Qt libraries" OFF)
option(UI_USE_STATIC_BOOST "Build with static Boost libraries" ON)
option(WITH_TESTS "Enable Tests" ON)
option(NEXTAPP_USE_GRPC_STREAMING_FLOW_CONTROL "Use gRPC flow control. If disabled, caches the stream in memory." OFF)

if (UI_USE_STATIC_BOOST)
    add_definitions(-DUSE_STATIC_BOOST)
endif()

if (NEXTAPP_USE_GRPC_STREAMING_FLOW_CONTROL)
    add_definitions(-DUSE_GRPC_STREAMING_FLOW_CONTROL)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(NEXTAPP_DEVEL_SETTINGS_DEFAULT ON)
    set(NEXTAPP_APP_NAME "NextAppDevel")
else()
    set(NEXTAPP_DEVEL_SETTINGS_DEFAULT OFF)
    set(NEXTAPP_APP_NAME "NextApp")
endif()

option(NEXTAPP_DEVEL_SETTINGS "Use '-devel' postfix for the settings location to use a different profile than normal." ${NEXTAPP_DEVEL_SETTINGS_DEFAULT})
if (NEXTAPP_DEVEL_SETTINGS)
    add_definitions(-DDEVEL_SETTINGS)
endif()
option(NEXTAPP_USE_ANDROID_UI "Use Android UI. Enable to develop Android UI on the Desktop." OFF)
if (NEXTAPP_USE_ANDROID_UI)
    add_definitions(-DUSE_ANDROID_UI)
endif()

if (ANDROID_BUILD)
    set(NEXTAPP_APP_DOMAIN "eu.lastviking" CACHE INTERNAL "")
    # Note, this require you to specify the location to this projects `google-services.json` in GOOGLE_SERVICES_PATH
    # This file contains secrets, and should never be checked in as part of the source code.
    # If you make your own version of the app, make sure you have set `NEXTAPP_APP_NAME` and `NEXTAPP_APP_DOMAIN` to
    # the same values as the package in you `google-services.json` file.
    option(NEXTAPP_WITH_FCM "Enable Google Firebase support" OFF)
    if (NEXTAPP_WITH_FCM)
        add_definitions(-DWITH_FCM)
        if (NOT GOOGLE_SERVICES_PATH)
            if(DEFINED ENV{GOOGLE_SERVICES_PATH})
                set(GOOGLE_SERVICES_PATH "$ENV{GOOGLE_SERVICES_PATH}")
            else()
                message(FATAL_ERROR "You must specify the path to google-services.json using -DGOOGLE_SERVICES_PATH=/path/to/google-services.json")
            endif()
        endif()
    endif()
endif()

if(USE_STATIC_QT)
    add_compile_definitions(QT_STATIC QT_STATICPLUGIN)
endif()

if (LINUX_BUILD OR WINDOWS_BUILD)
  find_package(gRPC CONFIG REQUIRED)
  find_package(Protobuf REQUIRED)
endif()

if (LINUX_BUILD)
    add_compile_definitions(LINUX_BUILD)
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    QuickControls2
    Graphs
    Svg
    Test
    Concurrent
    Test
    Sql
    Protobuf
    ProtobufQuick
    Grpc
    ProtobufQtCoreTypes
    ProtobufWellKnownTypes
)

if(ANDROID)
    message(STATUS "Compiling for Android")
    string(TOLOWER "${NEXTAPP_APP_NAME}" APP_LABEL)
    message(STATUS "APP_LABEL (lowercase): ${APP_LABEL}")
    set(APP_LIB "${PROJECT_NAME}")
    set(PACKAGE_NAME "${NEXTAPP_APP_DOMAIN}.${APP_LABEL}")
    string(REPLACE "." "_" PACKAGE_NAME_UNDERSCORES ${PACKAGE_NAME})
    set(ANDROID_OUT_DIR    "${CMAKE_BINARY_DIR}/android")
    file(MAKE_DIRECTORY    ${ANDROID_OUT_DIR})

    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/android/AndroidManifest.xml.in"
      "${ANDROID_OUT_DIR}/AndroidManifest.xml"
      @ONLY
    )

    file(COPY
      "${CMAKE_CURRENT_SOURCE_DIR}/android/res"
      DESTINATION ${ANDROID_OUT_DIR}
    )

if (NEXTAPP_WITH_FCM)
    set(FCM_SERVICE
    "<service
        android:name=\"${PACKAGE_NAME}.NextappFirebaseMessagingService\"
        android:exported=\"false\">
        <intent-filter>
            <action android:name=\"com.google.firebase.MESSAGING_EVENT\" />
        </intent-filter>
    </service>")

    file(COPY
      "${GOOGLE_SERVICES_PATH}"
      DESTINATION ${ANDROID_OUT_DIR}
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/android/build.gradle.fcm"
        "${ANDROID_OUT_DIR}/build.gradle"
        COPYONLY
    )
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/android/src/NextappFirebaseMessagingService.java.in"
      "${ANDROID_OUT_DIR}/src/eu/lastviking/${APP_LABEL}/NextappFirebaseMessagingService.java"
      @ONLY
    )

    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/android/AndroidFcmBridge.h.in"
      "${CMAKE_BINARY_DIR}/AndroidFcmBridge.h"
      @ONLY
    )

    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/android/AndroidFcmBridge.cpp.in"
      "${CMAKE_BINARY_DIR}/AndroidFcmBridge.cpp"
      @ONLY
    )

    set (FCM_SOURCES
        "${CMAKE_BINARY_DIR}/AndroidFcmBridge.cpp"
        "${CMAKE_BINARY_DIR}/AndroidFcmBridge.h")
endif()

    set(QT_ANDROID_PACKAGE_SOURCE_DIR "${ANDROID_OUT_DIR}")
    set(ANDROID_MIN_SDK_VERSION 31)  # Android 12
    set(ANDROID_TARGET_SDK_VERSION 35)
    add_definitions(-DQT_NO_STATX)
endif()


qt_standard_project_setup()

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0002 NEW)
qt_policy(SET QTP0003 NEW)
qt_policy(SET QTP0004 NEW)

if(USE_STATIC_QT)
    #set(Boost_USE_STATIC_LIBS ON)
    #set(OPENSSL_USE_STATIC_LIBS TRUE)
    set(BUILD_SHARED_LIBS OFF)
    #set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib .so .dll)
    find_package(Threads REQUIRED)
endif()


set(QCORO_BUILD_EXAMPLES OFF)
set(QCORO_BUILD_TESTING OFF)
set(QCORO_ENABLE_ASAN OFF)
set(QCORO_WITH_QTWEBSOCKETS OFF)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/qcoro
    ${CMAKE_BINARY_DIR}/qcoro_build)

find_package(QCoro6 COMPONENTS Core)
find_package(QCoro6 COMPONENTS Qml)
find_package(QCoro6 COMPONENTS Quick)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/expected/include)


#include(cmake/3rdparty.cmake)
# Use logfault from git submodule in stead of adding it
if (NOT TARGET logfault)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/logfault/include)
    add_library(logfault INTERFACE)
endif()


if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-D_DEBUG)
endif()

set (protofile_nextapp "${CMAKE_CURRENT_SOURCE_DIR}/../proto/nextapp.proto")
set (protofile_signup "${CMAKE_CURRENT_SOURCE_DIR}/../proto/signup.proto")
set (protofile_common "${CMAKE_CURRENT_SOURCE_DIR}/../proto/common.proto")

add_library(qt_grpc STATIC)

qt_add_protobuf(na_proto_lib
    QML
    QML_URI nextapp.pb
    PROTO_FILES
        ${protofile_nextapp}
        ${protofile_signup}
        ${protofile_common}
    PROTO_INCLUDES
        $<TARGET_PROPERTY:Qt6::ProtobufQtCoreTypes,QT_PROTO_INCLUDES>
)

if(USE_STATIC_QT)
    ## Stalins workaround for static Qt. This works, but will break soon enough
    ## target_include_directories(na_proto_lib
    ##   PRIVATE
    ##     /opt/qt-static/include/QtProtobufWellKnownTypes/6.8.3
    ## )
    ## pull the INTERFACE include‐dirs (public + private) out of the Qt target

    # This work-around for not linking to the Qt6::ProtobufWellKnownTypesPrivate target
    # is needed if you use static Qt libraries and well known protobuf types or `optional`
    # types in your proto files. Otherwise the generated code will not compile.
    # (Took me a few hours to figure this out)
    get_target_property(_wkt_inc_dirs
       Qt6::ProtobufWellKnownTypesPrivate
       INTERFACE_INCLUDE_DIRECTORIES
    )

    message(STATUS "WKT include dirs: ${_wkt_inc_dirs}")

    target_include_directories(na_proto_lib
        PRIVATE
          ${_wkt_inc_dirs}
    )
else()
  target_link_libraries(na_proto_lib
    PRIVATE
      Qt6::ProtobufWellKnownTypes
      Qt6::ProtobufWellKnownTypesPrivate
  )
endif()

qt_add_grpc(na_grpc_lib CLIENT
    PROTO_FILES ${protofile_nextapp} ${protofile_signup}
)

target_link_libraries(na_grpc_lib
    PRIVATE
        na_proto_lib
        Qt6::Core
#        Qt6::Protobuf
        Qt6::ProtobufQuick
        Qt6::ProtobufQtCoreTypes
        Qt6::Grpc
)

qt_add_executable(${PROJECT_NAME}
    main.cpp
    logging.h
)

target_include_directories(${PROJECT_NAME} PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include>
    PRIVATE src
)

# Check if std::format is available
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <format>
    #include <string>
    int main() {
        std::string s = std::format(\"Hello {}!\", \"World\");
        return 0;
    }
" HAS_STD_FORMAT)

include(FetchContent)

# Fetch the MiniAudio GitHub repository
FetchContent_Declare(
    MiniAudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG master  # You can change this to a specific commit or release tag
)

# Make the repository available
FetchContent_MakeAvailable(MiniAudio)

# If std::format is not available, fallback to fmtlib
if(NOT HAS_STD_FORMAT OR ANDROID OR APPLE)
   message(STATUS "std::format not available, using fmtlib...")
    message(STATUS "std::format maybe not available (hard to tell sometimes!), enabeling fmtlib...")
    FetchContent_Declare(
      fmt
      GIT_REPOSITORY https://github.com/fmtlib/fmt
      GIT_TAG        e69e5f977d458f2650bb346dadf2ad30c5320281) # 10.2.1
    FetchContent_MakeAvailable(fmt)
    include_directories(${fmt_SOURCE_DIR}/include)
    set(FMT_TARGET fmt)
    set(FMT_DEPS fmt::fmt)
endif()

if (NOT ANDROID)
    find_package(Boost ${USE_BOOST_VERSION} CONFIG REQUIRED)
    find_package(OpenSSL CONFIG QUIET)
    if(NOT OpenSSL_FOUND)
        find_package(OpenSSL REQUIRED)
    endif()

    target_include_directories(${PROJECT_NAME} PRIVATE ${Boost_INCLUDE_DIRS})
else()
    # TODO: Get the header only libraries we use from the system or download them
    set(NEXTAPP_BOOST_ROOT "/opt/boost/boost_1_88_0"
        CACHE STRING
        "Boost location if set manually (required for Android/ QT Creator)")
    target_include_directories(${PROJECT_NAME} PRIVATE ${NEXTAPP_BOOST_ROOT})

    FetchContent_Declare(
        android_openssl
        DOWNLOAD_EXTRACT_TIMESTAMP true
        URL https://github.com/KDAB/android_openssl/archive/refs/heads/master.zip
    )
    FetchContent_MakeAvailable(android_openssl)
    set(OPENSSL_INCLUDE "${android_openssl_SOURCE_DIR}/ssl_3/include")
    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
    add_android_openssl_libraries(${PROJECT_NAME})
    get_target_property(QT_ANDROID_EXTRA_LIBS_VALUE ${PROJECT_NAME} QT_ANDROID_EXTRA_LIBS)
    message("OPENSSL_INCLUDE: ${OPENSSL_INCLUDE}")
    target_include_directories(${PROJECT_NAME}
        PRIVATE
        ${OPENSSL_INCLUDE}
        ${CMAKE_BINARY_DIR}
    )
endif()

set_source_files_properties(qml/Colors.qml
    PROPERTIES
        QT_QML_SINGLETON_TYPE TRUE
)

if (APPLE)
    set (EXTRA_SOURCE_FILES AppDelegate.mm)
endif()

qt_add_qml_module(${PROJECT_NAME}
    URI NextAppUi
    VERSION ${VERSION}
    QML_FILES
        Main.qml
        qml/About.qml
        qml/ActionStatisticsView.qml
        qml/ActionStatsDlg.qml
        qml/ActionsList.qml
        qml/ActionsListFilterDlg.qml
        qml/ActionsListView.qml
        qml/CheckBoxWithFontIcon.qml
        qml/Colors.qml # x
        qml/CommonElements.qml
        qml/CurrentWorkSessionsView.qml
        qml/DevicesDlg.qml
        qml/DisabledDimmer.qml
        qml/DlgInputField.qml
        qml/EditActionDlg.qml
        qml/EditActionView.qml
        qml/EditNodeDlg.qml
        qml/EditWorkSession.qml
        qml/ImportExport.qml
        qml/MainTree.qml
        qml/MyMenu.qml # x
        qml/MyMenuBar.qml # x
        qml/NoInstance.qml
        qml/NotificationsView.qml
        qml/ReportsView.qml
        qml/ResizeButton.qml # x
        qml/SelectedIndicatorBar.qml
        qml/Sidebar.qml
        qml/SplitViewComponent.qml
        qml/StyledButton.qml
        qml/StyledCheckBox.qml
        qml/StyledComboBox.qml
        qml/StyledHeaderView.qml
        qml/StyledTabButton.qml
        qml/SynchPopup.qml
        qml/WeeklyHoursView.qml
        qml/WeeklyReview.qml
        qml/WeeklyReviewView.qml
        qml/WindowDragHandler.qml # x
        qml/WorkSessionList.qml
        qml/WorkSessionsView.qml
        qml/android/DrawerContent.qml
        qml/android/TodoList.qml
        qml/android/WorkSessionsCompactList.qml
        qml/android/WorkSessionsStacked.qml
        qml/android/main.qml
        qml/calendar/CalendarView.qml
        qml/calendar/CategoriesUsed.qml
        qml/calendar/CategoryUsedPopup.qml
        qml/calendar/DayPlan.qml
        qml/calendar/EditTimeBlockDlg.qml
        qml/calendar/ExpandArea.qml
        qml/calendar/HoursBar.qml
        qml/calendar/TimeBlockArea.qml
        qml/categories/CategoriesMgr.qml
        qml/categories/CategoryComboBox.qml
        qml/categories/ColorPicker.qml
        qml/categories/EditCategoryDlg.qml
        qml/common.js
        qml/components/DatePicker.qml
        qml/components/DifficultySelector.qml
        qml/components/MoveDue.qml
        qml/components/PrioritySelector.qml
        qml/components/SliderTicks.qml
        qml/components/TimeAndDurationInput.qml
        qml/components/TimeInput.qml
        qml/components/ToolBarBtn.qml
        qml/components/UnrecognizedDeviceErrorDlg.qml
        qml/components/WhenSelector.qml
        qml/greendays/DayDialog.qml
        qml/greendays/DaysInYear.qml
        qml/greendays/GdMonth.qml
        qml/greendays/GdQuarter.qml
        qml/log/LogDialog.qml
        qml/log/LogView.qml
        qml/onboard/AddDevicePage.qml
        qml/onboard/GetNewOtpForDevice.qml
        qml/onboard/OnBoardingWizard.qml
        qml/onboard/OnboardingAccept.qml
        qml/onboard/OnboardingAccount.qml
        qml/onboard/OnboardingReady.qml
        qml/onboard/OnboardingServer.qml
        qml/onboard/OnboardingWelcome.qml
        qml/settings/AdvancedSettings.qml
        qml/settings/DeleteAccountConfirmation.qml
        qml/settings/GlobalSettings.qml
        qml/settings/NotificationSettings.qml
        qml/settings/PrefSettings.qml
        qml/settings/ResetConfirmation.qml
        qml/settings/ServerSettings.qml
        qml/settings/SettingsDlg.qml
        qml/styling/SplitterStyle.qml
    RESOURCES
        "fonts/Font Awesome 6 Brands-Regular-400.otf"
        "fonts/Font Awesome 6 Free-Regular-400.otf"
        "fonts/Font Awesome 6 Free-Solid-900.otf"
        icons/filter.svg
        icons/folder.svg
        icons/folder_closed.svg
        icons/folder_open.svg
        icons/fontawsome/angle-down.svg
        icons/fontawsome/angle-right.svg
        icons/fontawsome/arrows-up-down-left-right.svg
        icons/fontawsome/bars.svg
        icons/fontawsome/calendar-day.svg
        icons/fontawsome/calendar-days.svg
        icons/fontawsome/calendar.svg
        icons/fontawsome/chart-line.svg
        icons/fontawsome/circle.svg
        icons/fontawsome/clock.svg
        icons/fontawsome/cloud-bolt.svg
        icons/fontawsome/file-export.svg
        icons/fontawsome/file-lines.svg
        icons/fontawsome/flag.svg
        icons/fontawsome/folder-tree.svg
        icons/fontawsome/folder.svg
        icons/fontawsome/gear.svg
        icons/fontawsome/hourglass-half.svg
        icons/fontawsome/laptop.svg
        icons/fontawsome/layer-group.svg
        icons/fontawsome/list-check.svg
        icons/fontawsome/pen-to-square.svg
        icons/fontawsome/rotate.svg
        icons/fontawsome/square-check.svg
        icons/fontawsome/square.svg
        icons/fontawsome/trash-can.svg
        icons/generic_file.svg
        icons/globe.svg
        icons/info_sign.svg
        icons/light_bulb.svg
        icons/multiselection-menu.svg
        icons/nextapp.svg
        icons/organization.svg
        icons/person.svg
        icons/project.svg
        icons/read.svg
        icons/refresh.svg
        icons/resize.svg
        icons/square-checked.svg
        icons/square-unchecked.svg
        icons/task.svg
        icons/weekly_review.svg
        sounds/387351__cosmicembers__simple-ding.wav
        sounds/515643__mashedtatoes2__ding2_edit.wav
        sounds/611112__5ro4__bell-ding-2.wav
    SOURCES
        ${EXTRA_SOURCE_FILES}
        ${FCM_SOURCES}
        ActionCategoriesModel.cpp
        ActionCategoriesModel.h
        ActionInfoCache.cpp
        ActionInfoCache.h
        ActionStatsModel.cpp
        ActionStatsModel.h
        ActionsModel.cpp
        ActionsModel.h
        ActionsOnCurrentCalendar.cpp
        ActionsOnCurrentCalendar.h
        ActionsWorkedOnTodayCache.cpp
        ActionsWorkedOnTodayCache.h
        AppInstanceMgr.cpp
        AppInstanceMgr.h
        CalendarCache.cpp
        CalendarCache.h
        CalendarDayModel.cpp
        CalendarDayModel.h
        CalendarModel.cpp
        CalendarModel.h
        CategoryUseModel.cpp
        CategoryUseModel.h
        DayColorModel.cpp
        DayColorModel.h
        DbStore.cpp
        DbStore.h
        DevicesModel.cpp
        DevicesModel.h
        DualView.h DualView.cpp
        GreenDayModel.cpp
        GreenDayModel.h
        GreenDaysModel.cpp
        GreenDaysModel.h
        GreenMonthModel.cpp
        GreenMonthModel.h
        GrpcIncomingStream.h
        ImportExportModel.cpp
        LogModel.cpp
        LogModel.h
        MainTreeModel.cpp
        MainTreeModel.h
        MaterialDesignStyling.cpp
        MaterialDesignStyling.h
        ModelInstances.cpp
        ModelInstances.h
        NextAppCore.cpp
        NextAppCore.h
        NotificationsModel.cpp
        NotificationsModel.h
        OtpModel.cpp
        OtpModel.h
        ReviewModel.cpp
        ReviewModel.h
        ServerComm.cpp
        ServerComm.h
        ServerSynchedCahce.h
        SoundPlayer.cpp
        SoundPlayer.h
        TimeBoxActionsModel.cpp
        TimeBoxActionsModel.h
        UseCaseTemplates.cpp
        UseCaseTemplates.h
        WeeklyWorkReportModel.cpp
        WeeklyWorkReportModel.h
        WorkCache.cpp
        WorkCache.h
        WorkModel.cpp
        WorkModel.h
        WorkModelBase.cpp
        WorkModelBase.h
        WorkSessionsModel.cpp
        WorkSessionsModel.h
        format_wrapper.h
        nextapp.h
        util.cpp
        util.h
)


if (APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER eu.lastviking.nextapp
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
        MACOSX_BUNDLE TRUE
        RUNTIME_OUTPUT_NAME nextapp
    )
elseif (WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
        RUNTIME_OUTPUT_NAME nextapp
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
elseif (ANDROID)
    # set_property(TARGET ${PROJECT_NAME} PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
    #              ${CMAKE_CURRENT_SOURCE_DIR}/android)

    set_property(TARGET ${PROJECT_NAME}
          PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${ANDROID_OUT_DIR}")

    target_compile_definitions(${PROJECT_NAME} PUBLIC __ANDROID__)
    include(GNUInstallDirs)
    install(TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION .
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
else()
    # Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
    # If you are developing for iOS or macOS you should consider setting an
    # explicit, fixed bundle identifier manually though.
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_NAME nextapp
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
endif()

if (ANDROID)
    set(OPEN_SSL_DEPS ${QT_ANDROID_EXTRA_LIBS_VALUE})
else ()
    set(OPEN_SSL_DEPS OpenSSL::SSL OpenSSL::Crypto)
endif()

add_dependencies(${PROJECT_NAME} logfault na_grpc_lib ${OPENSSL_TARGET} ${FMT_TARGET})

if(USE_STATIC_QT)
    qt_import_plugins(${PROJECT_NAME}
    #qt6_import_qt_plugins(${PROJECT_NAME}
        PLUGINS
            qxcb
            # Other plugins as required
        STATIC
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        QT_QPA_STATIC
        )
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        na_proto_lib
        na_grpc_lib
        QCoro6::Core
        QCoro6::Qml
        QCoro6::Quick
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Graphs
        Qt6::Svg
        Qt6::Protobuf
        Qt6::Grpc
        Qt6::Concurrent
        Qt6::Test
        Qt6::Sql
        $<$<OR:$<BOOL:${WINDOWS_BUILD}>,$<BOOL:${LINUX_BUILD}>>:gRPC::grpc++>
        $<$<BOOL:${LINUX_BUILD}>:Qt6::DBus>
        $<$<AND:$<BOOL:${USE_STATIC_QT}>,$<BOOL:${LINUX_BUILD}>>:dl>
        ${OPEN_SSL_DEPS}
        ${FMT_DEPS}
        miniaudio
)

if (APPLE)
    set(PLIST_IN   "${NEXTAPP_ROOT}/building/macos/Info.plist.in")
    set(PLIST_OUT  "${CMAKE_BINARY_DIR}/Info.plist")
    configure_file("${PLIST_IN}" "${PLIST_OUT}" @ONLY)

    set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE            ON
    MACOSX_BUNDLE_INFO_PLIST "${PLIST_OUT}"
    )
        
    install(TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION .
        COMPONENT Application
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(
      FILES "${NEXTAPP_ROOT}/building/macos/NextApp.icns"
      DESTINATION "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources"
      COMPONENT Application
    )
    install(
        FILES "${NEXTAPP_ROOT}/src/NextAppUi/LICENSE"
        DESTINATION "."
        COMPONENT Application
    )

elseif(WIN32)
    install(TARGETS ${PROJECT_NAME}
        COMPONENT Application
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

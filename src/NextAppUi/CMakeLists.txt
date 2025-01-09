cmake_minimum_required(VERSION 3.16)

project(appNextAppUi VERSION 0.1.30 LANGUAGES CXX)
add_definitions(-DNEXTAPP_UI_VERSION=\"${appNextAppUi_VERSION}\")

set(NEXTAPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(SERVER_ADDRESS "https://signup.ws.dev.next-app.org:10322" CACHE STRING "Default address to signup server")
add_definitions("-DSERVER_ADDRESS=\"${SERVER_ADDRESS}\"")

option(NEXTAPP_WITH_TREE_MODEL_TESTING "Enable ongoing tests for the MainTreeModel" OFF)
if (NEXTAPP_WITH_TREE_MODEL_TESTING)
    add_definitions(-DWITH_TREE_MODEL_TESTING)
endif()

option(USE_STATIC_QT "Build with static Qt libraries" OFF)
option(WITH_TESTS "Enable Tests" ON)
option(NEXTAPP_USE_GRPC_STREAMING_FLOW_CONTROL "Use gRPC flow control. If disabled, caches the stream in memory." OFF)

# if(USE_STATIC_QT)
#     set(BUILD_SHARED_LIBS OFF)
#     set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib .so .dll)
# else()
#     set(BUILD_SHARED_LIBS ON)
# endif()

if (NEXTAPP_USE_GRPC_STREAMING_FLOW_CONTROL)
    add_definitions(-DUSE_GRPC_STREAMING_FLOW_CONTROL)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(NEXTAPP_DEVEL_SETTINGS_DEFAULT ON)
else()
    set(NEXTAPP_DEVEL_SETTINGS_DEFAULT OFF)
endif()

option(NEXTAPP_DEVEL_SETTINGS "Use '-devel' postfix for the settings location to use a different profile than normal." ${NEXTAPP_DEVEL_SETTINGS_DEFAULT})
if (NEXTAPP_DEVEL_SETTINGS)
    add_definitions(-DDEVEL_SETTINGS)
endif()
option(NEXTAPP_USE_ANDROID_UI "Use Android UI. Enable to develop Android UI on the Desktop." OFF)
if (NEXTAPP_USE_ANDROID_UI)
    add_definitions(-DUSE_ANDROID_UI)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#set(QT6_IS_SHARED_LIBS_BUILD OFF)
if(USE_STATIC_QT)
    # Use target_compile_definitions instead of add_definitions for modern CMake
    add_compile_definitions(QT_STATIC QT_STATICPLUGIN)
endif()


find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Charts Svg Test Grpc Concurrent Test Multimedia Sql)

if(Qt6Core_VERSION VERSION_LESS "6.8.0")
    message("Using an old QT version. 6.8 is required for the current grpc plugin")
    find_package(Qt6 REQUIRED CONFIG COMPONENTS Protobuf)
else ()
    find_package(Qt6 REQUIRED CONFIG COMPONENTS ProtobufQuick)
    set (PROTOBUF_QML_DEPENDENCY Qt::ProtobufQuick)
endif()

qt_policy(
    SET QTP0001 NEW
)

qt_standard_project_setup()

if(USE_STATIC_QT)
    set(FFMPEG_MEDIAPLUGIN_LIB "${CMAKE_PREFIX_PATH}/plugins/multimedia/libffmpegmediaplugin.a")
    find_package(FFMPEG REQUIRED)
    message("FFMPEG_LIBRARIES: ${FFMPEG_LIBRARIES}")

    # There is some tiems a problem with the list, linking fails with:  error: no such file or directory: '$<1:optimized'
    if("optimized" IN_LIST FFMPEG_LIBRARIES)
        set(OPTIMIZED_FFMPEG_LIBRARIES "")
        set(NEXT_IS_OPTIMIZED FALSE)

        foreach(item IN LISTS FFMPEG_LIBRARIES)
            if(item STREQUAL "optimized")
                set(NEXT_IS_OPTIMIZED TRUE)
            elseif(item STREQUAL "debug")
                set(NEXT_IS_OPTIMIZED FALSE)
            else()
                if(NEXT_IS_OPTIMIZED)
                    list(APPEND OPTIMIZED_FFMPEG_LIBRARIES "${item}")
                endif()
            endif()
        endforeach()
    else()
        # If "optimized" is not found, use FFMPEG_LIBRARIES directly
        set(OPTIMIZED_FFMPEG_LIBRARIES "${FFMPEG_LIBRARIES}")
    endif()

    # Print the optimized libraries for verification
    message(STATUS "Optimized Libraries: ${OPTIMIZED_FFMPEG_LIBRARIES}")

    set(Boost_USE_STATIC_LIBS ON)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib .so .dll)
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

qt_add_protobuf(NextappProtoMessageLib
    QML
    QML_URI nextapp.pb
    PROTO_FILES
        ${protofile_nextapp}
        ${protofile_signup}
        ${protofile_common}
)

qt_add_grpc(NextappGrpcClient CLIENT
    PROTO_FILES ${protofile_nextapp} ${protofile_signup}
)

target_link_libraries(NextappGrpcClient
    PRIVATE
        NextappProtoMessageLib
        Qt6::Core
        Qt6::Protobuf
        ${PROTOBUF_QML_DEPENDENCY}
        Qt6::Grpc
)

qt_add_executable(${PROJECT_NAME}
    main.cpp
    logging.h
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(${PROJECT_NAME} PRIVATE LINUX_BUILD)
    set (LINUX_BUILD ON)
endif()

if(ANDROID)
    set(ANDROID_MIN_SDK_VERSION 31)  # Android 12
    set(ANDROID_TARGET_SDK_VERSION 35)
    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/android")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${ANDROID_PACKAGE_SOURCE_DIR}")
    add_definitions(-DQT_NO_STATX)
endif()


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

# If std::format is not available, fallback to fmtlib
if(NOT HAS_STD_FORMAT OR ANDROID)
    message(STATUS "std::format not available, using fmtlib...")
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
    find_package(Boost CONFIG REQUIRED)
    #find_package(OpenSSL CONFIG REQUIRED)

    find_package(OpenSSL CONFIG QUIET)
    if(NOT OpenSSL_FOUND)
        find_package(OpenSSL REQUIRED)
    endif()

    target_include_directories(${PROJECT_NAME} PRIVATE ${Boost_INCLUDE_DIRS})
else()
    target_include_directories(${PROJECT_NAME} PRIVATE /opt/boost/boost_1_87_0)

    FetchContent_Declare(
        android_openssl
        DOWNLOAD_EXTRACT_TIMESTAMP true
        URL https://github.com/KDAB/android_openssl/archive/refs/heads/master.zip
    )
    FetchContent_MakeAvailable(android_openssl)
    set(OPENSSL_INCLUDE "${android_openssl_SOURCE_DIR}/ssl_3/include")
    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
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
        qml/DevicesDlg.qml
        qml/DisabledDimmer.qml
        qml/DlgInputField.qml
        qml/EditActionDlg.qml
        qml/EditActionView.qml
        qml/EditNodeDlg.qml
        qml/EditWorkSession.qml
        qml/MainTree.qml
        qml/MyMenu.qml # x
        qml/MyMenuBar.qml # x
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
        qml/calendar/TimeBlock.qml
        qml/categories/CategoriesMgr.qml
        qml/categories/CategoryComboBox.qml
        qml/categories/ColorPicker.qml
        qml/categories/EditCategoryDlg.qml
        qml/common.js
        qml/components/DatePicker.qml
        qml/components/TimeAndDurationInput.qml
        qml/components/TimeInput.qml
        qml/greendays/DayDialog.qml
        qml/greendays/DaysInYear.qml
        qml/greendays/GdMonth.qml
        qml/greendays/GdQuarter.qml
        qml/onboard/AddDevicePage.qml
        qml/onboard/GetNewOtpForDevice.qml
        qml/onboard/OnBoardingWizard.qml
        qml/onboard/OnboardingAccept.qml
        qml/onboard/OnboardingAccount.qml
        qml/onboard/OnboardingReady.qml
        qml/onboard/OnboardingServer.qml
        qml/onboard/OnboardingWelcome.qml
        qml/settings/AdvancedSettings.qml
        qml/settings/GlobalSettings.qml
        qml/settings/NotificationSettings.qml
        qml/settings/PrefSettings.qml
        qml/settings/ServerSettings.qml
        qml/settings/SettingsDlg.qml
        qml/styling/SplitterStyle.qml
    RESOURCES
        "fonts/Font Awesome 6 Brands-Regular-400.otf"
        "fonts/Font Awesome 6 Free-Regular-400.otf"
        "fonts/Font Awesome 6 Free-Solid-900.otf"
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
        ActionCategoriesModel.cpp
        ActionCategoriesModel.h
        ActionInfoCache.cpp
        ActionInfoCache.h
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
        MainTreeModel.cpp
        MainTreeModel.h
        MaterialDesignStyling.cpp
        MaterialDesignStyling.h
        NextAppCore.cpp
        NextAppCore.h
        OtpModel.cpp
        OtpModel.h
        ReviewModel.cpp
        ReviewModel.h
        ServerComm.cpp
        ServerComm.h
        ServerSynchedCahce.h
        TimeBoxActionsModel.cpp
        TimeBoxActionsModel.h
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

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

if (APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER eu.lastviking.${PROJECT_NAME}
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
elseif (NOT ANDROID)
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
    message(STATUS "Compiling for Android")
    # QT_ANDROID_BUILD_ALL_ABI=ON
    # set(CMAKE_ANDROID_ABI arm64-v8a;armeabi-v7a;x86;x86_64)


    add_android_openssl_libraries(${PROJECT_NAME})
    get_target_property(QT_ANDROID_EXTRA_LIBS_VALUE ${PROJECT_NAME} QT_ANDROID_EXTRA_LIBS)

    message("OPENSSL_INCLUDE: ${OPENSSL_INCLUDE}")
    target_include_directories(${PROJECT_NAME} PRIVATE ${OPENSSL_INCLUDE})
    set(OPEN_SSL_DEPS ${QT_ANDROID_EXTRA_LIBS_VALUE})
else ()
    set(OPEN_SSL_DEPS OpenSSL::SSL OpenSSL::Crypto)
endif()

add_dependencies(${PROJECT_NAME} logfault NextappGrpcClient NextappProtoMessageLib ${OPENSSL_TARGET} ${FMT_TARGET})

if(USE_STATIC_QT)
    qt_import_plugins(${PROJECT_NAME}
    #qt6_import_qt_plugins(${PROJECT_NAME}
        PLUGINS
            ffmpegmediaplugin
            qxcb
            # Other plugins as required
        STATIC
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        QT_MULTIMEDIA_PLUGIN=ffmpeg
        QT_QPA_STATIC
        )
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        NextappGrpcClient
        NextappProtoMessageLib
        QCoro6::Core
        QCoro6::Qml
        QCoro6::Quick
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Charts
        Qt6::Svg
        Qt6::Protobuf
        Qt6::Grpc
        Qt6::Concurrent
        Qt6::Test
        Qt6::Multimedia
        Qt6::Sql
        $<$<BOOL:${LINUX_BUILD}>:Qt6::DBus>
        #Qt6::QJpegPlugin
        # QT insists on linking FFMPEG_MEDIAPLUGIN_LIB after the ffmpeg libraries it depends on,
        # which don't work with static linking. So we have to add it manually before it's dependencies.
        $<$<BOOL:${USE_STATIC_QT}>:${FFMPEG_MEDIAPLUGIN_LIB}>
        $<$<BOOL:${USE_STATIC_QT}>:${OPTIMIZED_FFMPEG_LIBRARIES}>
        $<$<BOOL:${USE_STATIC_QT}>:dl>
        ${OPEN_SSL_DEPS}
        ${FMT_DEPS}
)


if(ANDROID)
    target_compile_definitions(${PROJECT_NAME} PUBLIC __ANDROID__)
    include(GNUInstallDirs)
    install(TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION .
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
elseif(APPLE)
    install(TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION /Applications
        RUNTIME DESTINATION bin
    )
else()
    include(InstallRequiredSystemLibraries)
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGE_NAME "nextapp")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_CONTACT "jgaa@jgaa.com")

    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
    install(DIRECTORY ${CMAKE_PREFIX_PATH}/lib DESTINATION lib)

    include(CPack)
endif()


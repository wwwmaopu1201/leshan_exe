cmake_minimum_required(VERSION 3.21)
project(boer_lan_qt_server VERSION 2.1.11 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network Sql Widgets)
add_subdirectory(../common ${CMAKE_CURRENT_BINARY_DIR}/common)

add_executable(boer-qt-server
    src/main.cpp
    src/customer_ui_regression.cpp
    src/database.cpp
    src/database.h
    src/api_server.cpp
    src/api_server.h
    src/main_window.cpp
    src/main_window.h
    src/device_server.cpp
    src/device_server.h
    src/external_db.cpp
    src/external_db.h
    src/trial_state.cpp
    src/trial_state.h
    src/pattern_pages.cpp
    src/pattern_pages.h
    src/editor_dialog.cpp
    src/editor_dialog.h
    resources.qrc
)
set_target_properties(boer-qt-server PROPERTIES OUTPUT_NAME bohr-qt-server)
target_link_libraries(boer-qt-server PRIVATE Qt6::Core Qt6::Network Qt6::Sql Qt6::Widgets boer-qt-ui)
if(WIN32)
    set_target_properties(boer-qt-server PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(boer-qt-server PRIVATE windows.rc)
    set(BOER_WINDOWS_PRODUCT "Bohr LAN Server")
    set(BOER_WINDOWS_EXECUTABLE "bohr-qt-server.exe")
    configure_file(../common/windows-version.rc.in windows-version.rc @ONLY)
    target_sources(boer-qt-server PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows-version.rc")
    target_link_libraries(boer-qt-server PRIVATE shell32)
endif()
if(APPLE)
    set(app_icon "${CMAKE_CURRENT_SOURCE_DIR}/assets/app-icon.icns")
    set_source_files_properties("${app_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(boer-qt-server PRIVATE "${app_icon}")
    set_target_properties(boer-qt-server PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE "app-icon.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER com.boer.qtserver.prototype
        MACOSX_BUNDLE_BUNDLE_NAME "Bohr Qt Server")
endif()

enable_testing()
add_executable(boer-external-sync-probe EXCLUDE_FROM_ALL tests/external_sync_probe.cpp src/external_db.cpp)
target_include_directories(boer-external-sync-probe PRIVATE src)
target_link_libraries(boer-external-sync-probe PRIVATE Qt6::Core Qt6::Network Qt6::Sql)
add_test(NAME smoke COMMAND boer-qt-server --self-test --db ${CMAKE_CURRENT_BINARY_DIR}/smoke.db)
add_test(NAME trial_smoke COMMAND boer-qt-server --trial-self-test)
add_test(NAME ui_smoke COMMAND boer-qt-server --ui-self-test --db ${CMAKE_CURRENT_BINARY_DIR}/ui-smoke.db)
set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME customer_ui_regression COMMAND boer-qt-server --customer-ui-regression)
set_tests_properties(customer_ui_regression PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

target_compile_definitions(boer-qt-server PRIVATE BOER_APP_VERSION="${PROJECT_VERSION}")
add_test(NAME online_update COMMAND boer-online-update-test)
set_tests_properties(online_update PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME window_chrome COMMAND boer-window-chrome-test)
set_tests_properties(window_chrome PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME button_feedback COMMAND boer-button-feedback-test)
set_tests_properties(button_feedback PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME update_progress COMMAND boer-update-progress-test)
set_tests_properties(update_progress PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME update_flow COMMAND boer-online-update-ui-test)
set_tests_properties(update_flow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

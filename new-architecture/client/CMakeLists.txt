cmake_minimum_required(VERSION 3.21)
project(boer_lan_qt_client VERSION 2.1.11 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network Widgets)
add_subdirectory(../common ${CMAKE_CURRENT_BINARY_DIR}/common)

add_executable(boer-qt-client
    src/main.cpp
    src/login_regression.cpp
    src/catalog_regression.cpp
    src/statistics_regression.cpp
    src/interaction_regression.cpp
    src/network_resilience_regression.cpp
    src/pattern_regression.cpp
    src/api_client.cpp
    src/api_client.h
    src/client_file_dialog.cpp
    src/client_file_dialog.h
    src/excel_export.cpp
    src/excel_export.h
    src/client_window.cpp
    src/client_window.h
    src/pages.cpp
    src/pages.h
    src/page_widgets.cpp
    src/page_widgets.h
    src/list_page.cpp
    src/list_page.h
    src/overview_pages.cpp
    src/overview_pages.h
    src/form_dialog.cpp
    src/form_dialog.h
    src/management_pages.cpp
    src/management_pages.h
    src/remote_monitor_page.cpp
    src/remote_monitor_page.h
    src/direct_pattern_regression.cpp
    src/direct_pattern_dialog.cpp
    src/direct_pattern_dialog.h
    src/pattern_pages.cpp
    src/pattern_pages.h
    src/statistics_pages.cpp
    src/statistics_pages.h
    src/profile_support_pages.cpp
    src/profile_support_pages.h
    resources.qrc
)
set_target_properties(boer-qt-client PROPERTIES OUTPUT_NAME bohr-qt-client)
target_link_libraries(boer-qt-client PRIVATE Qt6::Core Qt6::Network Qt6::Widgets boer-qt-ui)
if(WIN32)
    set_target_properties(boer-qt-client PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(boer-qt-client PRIVATE windows.rc)
    set(BOER_WINDOWS_PRODUCT "Bohr LAN Client")
    set(BOER_WINDOWS_EXECUTABLE "bohr-qt-client.exe")
    configure_file(../common/windows-version.rc.in windows-version.rc @ONLY)
    target_sources(boer-qt-client PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows-version.rc")
endif()
if(APPLE)
    set(app_icon "${CMAKE_CURRENT_SOURCE_DIR}/assets/client-icon.icns")
    set_source_files_properties("${app_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(boer-qt-client PRIVATE "${app_icon}")
    set_target_properties(boer-qt-client PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Bohr Qt Client"
        MACOSX_BUNDLE_ICON_FILE "client-icon.icns"
        MACOSX_BUNDLE_BUNDLE_IDENTIFIER com.boer.qtclient)
endif()

enable_testing()
add_test(NAME ui_smoke COMMAND boer-qt-client --ui-self-test)
add_test(NAME excel_smoke COMMAND boer-qt-client --excel-self-test)
add_test(NAME modal_smoke COMMAND boer-qt-client --modal-self-test)
add_test(NAME customer_login_home COMMAND boer-qt-client --login-regression-test)
add_test(NAME customer_charts COMMAND boer-qt-client --chart-regression-test)
set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(excel_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(modal_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(customer_login_home customer_charts PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;BOER_CUSTOMER_ROW_LOG=${CMAKE_CURRENT_BINARY_DIR}/customer-row-results.jsonl"
    TIMEOUT 60)

target_compile_definitions(boer-qt-client PRIVATE BOER_APP_VERSION="${PROJECT_VERSION}")
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

add_test(NAME direct_pattern COMMAND boer-qt-client --direct-pattern-regression-test)
set_tests_properties(direct_pattern PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

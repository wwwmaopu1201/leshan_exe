add_library(boer-qt-ui STATIC
    window_chrome.cpp
    window_chrome.h
    update_progress_dialog.cpp
    update_progress_dialog.h
    online_update.cpp
    online_update.h
    interaction_compatibility.cpp
    interaction_compatibility.h
    window_compatibility.cpp
    window_compatibility.h
    ../server/src/ui_controls.cpp
    ../server/src/ui_controls.h
    ../server/src/ui_dialogs.cpp
    ../server/src/ui_dialogs.h
    ../server/src/ui_i18n.cpp
    ../server/src/ui_i18n.h
)
target_include_directories(boer-qt-ui PUBLIC ../server/src ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(boer-qt-ui PUBLIC Qt6::Core Qt6::Widgets Qt6::Network)
if(APPLE)
    enable_language(OBJCXX)
    target_sources(boer-qt-ui PRIVATE ui_modal_mac.mm)
    target_link_libraries(boer-qt-ui PUBLIC "-framework AppKit")
endif()

add_executable(boer-online-update-test online_update_test.cpp)
target_link_libraries(boer-online-update-test PRIVATE boer-qt-ui)

add_executable(boer-window-chrome-test window_chrome_test.cpp)
target_link_libraries(boer-window-chrome-test PRIVATE boer-qt-ui)

add_executable(boer-button-feedback-test button_feedback_test.cpp)
target_link_libraries(boer-button-feedback-test PRIVATE boer-qt-ui)

add_executable(boer-update-progress-test update_progress_test.cpp)
target_link_libraries(boer-update-progress-test PRIVATE boer-qt-ui)

add_executable(boer-online-update-ui-test online_update_ui_test.cpp)
target_link_libraries(boer-online-update-ui-test PRIVATE boer-qt-ui)

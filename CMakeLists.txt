cmake_minimum_required(VERSION 3.16)

project(QTrans LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(llama CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_library(qtrans_engine STATIC
    src/storage/app_paths.cpp
    src/storage/settings.cpp
    src/model/model_catalog.cpp
    src/model/model_files.cpp
    src/network/download.cpp
    src/translation/translation_languages.cpp
    src/translation/translation_model.cpp
    src/translation/hymt.cpp
    src/translation/inference_engine.cpp
    src/task/task_queue.cpp
    src/task/task_orchestrator.cpp
)

target_include_directories(qtrans_engine PUBLIC src)
target_link_libraries(qtrans_engine PUBLIC llama CURL::libcurl)

add_executable(QTrans
    src/app/main.cpp
    src/app/mainwindow.cpp
    src/app/mainwindow.h
    src/app/task_service.cpp
    src/app/task_service.h
    src/app/modal_overlay.cpp
    src/app/modal_overlay.h
    src/app/ui/alert_panel.cpp
    src/app/ui/alert_panel.h
    src/app/ui/app_theme.cpp
    src/app/ui/app_theme.h
    src/app/ui/download_progress_panel.cpp
    src/app/ui/download_progress_panel.h
    src/app/ui/image_utils.cpp
    src/app/ui/image_utils.h
    src/app/ui/model_missing_panel.cpp
    src/app/ui/model_missing_panel.h
    src/app/ui/model_page.cpp
    src/app/ui/model_page.h
    src/app/ui/sidebar_widget.cpp
    src/app/ui/sidebar_widget.h
    src/app/ui/translate_page.cpp
    src/app/ui/translate_page.h
    src/app/widget_utils.cpp
    src/app/widget_utils.h
    src/wordselect/popup_window.cpp
    src/wordselect/popup_window.h
    src/wordselect/hotkey_manager.cpp
    src/wordselect/hotkey_manager.h
    src/wordselect/clipboard_capture.cpp
    src/wordselect/clipboard_capture.h
    src/wordselect/session_controller.cpp
    src/wordselect/session_controller.h
    src/wordselect/system_tray.cpp
    src/wordselect/system_tray.h
    resources/qtrans.qrc
)

target_link_libraries(QTrans PRIVATE qtrans_engine Qt6::Widgets)

add_executable(ai_test
    test_direct.cpp
)
target_link_libraries(ai_test PRIVATE qtrans_engine Qt6::Core)

if(APPLE)
    target_link_libraries(QTrans PRIVATE "-framework Carbon")
    qt_import_plugins(QTrans
        INCLUDE BY_TYPE platforms Qt6::QCocoaIntegrationPlugin
        INCLUDE BY_TYPE platformthemes Qt6::QMacStylePlugin
    )
endif()

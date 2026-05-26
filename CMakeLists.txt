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

set(QTRANS_SOURCES_COMMON
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
    src/app/ui/wordselect_page.cpp
    src/app/ui/wordselect_page.h
    src/app/widget_utils.cpp
    src/app/widget_utils.h
    src/wordselect/popup_window.cpp
    src/wordselect/popup_window.h
    src/wordselect/clipboard_capture.cpp
    src/wordselect/clipboard_capture.h
    src/wordselect/hotkey_manager.h
    src/wordselect/session_controller.cpp
    src/wordselect/session_controller.h
    src/wordselect/system_tray.cpp
    src/wordselect/system_tray.h
    resources/qtrans.qrc
)

set(QTRANS_SOURCES_WIN
    src/wordselect/win/clipboard_capture.cpp
    src/wordselect/win/hotkey_manager.cpp
)

set(QTRANS_SOURCES_MAC
    src/wordselect/mac/clipboard_capture.cpp
    src/wordselect/mac/hotkey_manager.cpp
    src/wordselect/mac/platform_utils.cpp
    src/wordselect/mac/platform_utils.h
)

add_executable(QTrans ${QTRANS_SOURCES_COMMON})

target_link_libraries(QTrans PRIVATE qtrans_engine Qt6::Widgets)

if(WIN32)
    target_sources(QTrans PRIVATE ${QTRANS_SOURCES_WIN})
elseif(APPLE)
    target_sources(QTrans PRIVATE ${QTRANS_SOURCES_MAC})
    target_link_libraries(QTrans PRIVATE "-framework Carbon" "-framework CoreGraphics")
    qt_import_plugins(QTrans
        INCLUDE BY_TYPE platforms Qt6::QCocoaIntegrationPlugin
        INCLUDE BY_TYPE platformthemes Qt6::QMacStylePlugin
    )
endif()

if(MINGW)
    target_link_options(QTrans PRIVATE -static-libgcc -static-libstdc++ -static -mwindows)
endif()

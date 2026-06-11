cmake_minimum_required(VERSION 3.16)

project(QTrans LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(llama CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc i18n data)
find_package(simdutf CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "^x64-mingw")
    set(QTRANS_MULTI_BACKEND_VALUE 1)
else()
    set(QTRANS_MULTI_BACKEND_VALUE 0)
endif()

if(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "osx")
    set(QTRANS_GPU_METAL_VALUE 1)
else()
    set(QTRANS_GPU_METAL_VALUE 0)
endif()

option(QTRANS_BUILD_TESTS "Build unit tests" OFF)

if(QTRANS_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(core)

add_library(qtrans_engine STATIC
    apps/desktop/src/storage/app_paths.cpp
    apps/desktop/src/storage/settings.cpp
    apps/desktop/src/model/inference_backend.cpp
    apps/desktop/src/model/inference_resolver.cpp
    apps/desktop/src/model/model_catalog.cpp
    apps/desktop/src/model/model_files.cpp
    apps/desktop/src/model/platform_profile.cpp
    apps/desktop/src/model/runtime_capabilities.cpp
    apps/desktop/src/network/download.cpp
    apps/desktop/src/text/utf8.cpp
    apps/desktop/src/text/utf8_stream_buffer.cpp
    apps/desktop/src/text/sentence_splitter.cpp
    apps/desktop/src/text/chunker.cpp
    apps/desktop/src/log/init.cpp
    apps/desktop/src/log/logger.cpp
    apps/desktop/src/log/registry.cpp
    apps/desktop/src/log/component.cpp
    apps/desktop/src/log/ai_trace.cpp
    apps/desktop/src/log/console_progress.cpp
    apps/desktop/src/translation/translation_languages.cpp
    apps/desktop/src/translation/translation_model.cpp
    apps/desktop/src/translation/hymt.cpp
    apps/desktop/src/translation/inference_engine.cpp
    apps/desktop/src/task/task_queue.cpp
    apps/desktop/src/task/task_orchestrator.cpp
)

target_include_directories(qtrans_engine PUBLIC apps/desktop/src)
target_link_libraries(qtrans_engine PUBLIC qtrans_core)

target_link_libraries(qtrans_engine PUBLIC
    llama
    CURL::libcurl
    ICU::uc
    ICU::i18n
    ICU::data
    simdutf::simdutf
    spdlog::spdlog
)

target_compile_definitions(qtrans_engine PUBLIC
    $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_WARN>
)

if(QTRANS_MULTI_BACKEND_VALUE)
    target_compile_definitions(qtrans_engine PUBLIC QTRANS_MULTI_BACKEND)
endif()

if(QTRANS_GPU_METAL_VALUE)
    target_compile_definitions(qtrans_engine PUBLIC QTRANS_GPU_METAL)
endif()

set(QTRANS_SOURCES_COMMON
    apps/desktop/src/app/string_bridge.cpp
    apps/desktop/src/app/main.cpp
    apps/desktop/src/app/single_instance.cpp
    apps/desktop/src/app/single_instance.h
    apps/desktop/src/app/mainwindow.cpp
    apps/desktop/src/app/mainwindow.h
    apps/desktop/src/app/task_service.cpp
    apps/desktop/src/app/task_service.h
    apps/desktop/src/app/modal_overlay.cpp
    apps/desktop/src/app/modal_overlay.h
    apps/desktop/src/app/ui/alert_panel.cpp
    apps/desktop/src/app/ui/alert_panel.h
    apps/desktop/src/app/ui/app_theme.cpp
    apps/desktop/src/app/ui/app_theme.h
    apps/desktop/src/app/ui/download_progress_panel.cpp
    apps/desktop/src/app/ui/download_progress_panel.h
    apps/desktop/src/app/ui/image_utils.cpp
    apps/desktop/src/app/ui/image_utils.h
    apps/desktop/src/app/ui/model_missing_panel.cpp
    apps/desktop/src/app/ui/model_missing_panel.h
    apps/desktop/src/app/ui/model_page.cpp
    apps/desktop/src/app/ui/model_page.h
    apps/desktop/src/app/ui/sidebar_widget.cpp
    apps/desktop/src/app/ui/sidebar_widget.h
    apps/desktop/src/app/ui/translate_page.cpp
    apps/desktop/src/app/ui/translate_page.h
    apps/desktop/src/app/ui/wordselect_page.cpp
    apps/desktop/src/app/ui/wordselect_page.h
    apps/desktop/src/app/widget_utils.cpp
    apps/desktop/src/app/widget_utils.h
    apps/desktop/src/wordselect/popup_window.cpp
    apps/desktop/src/wordselect/popup_window.h
    apps/desktop/src/wordselect/clipboard_capture.cpp
    apps/desktop/src/wordselect/clipboard_capture.h
    apps/desktop/src/wordselect/hotkey_manager.h
    apps/desktop/src/wordselect/session_controller.cpp
    apps/desktop/src/wordselect/session_controller.h
    apps/desktop/src/wordselect/system_tray.cpp
    apps/desktop/src/wordselect/system_tray.h
    apps/desktop/resources/qtrans.qrc
)

set(QTRANS_SOURCES_WIN
    apps/desktop/src/wordselect/win/clipboard_capture.cpp
    apps/desktop/src/wordselect/win/hotkey_manager.cpp
)

set(QTRANS_SOURCES_MAC
    apps/desktop/src/wordselect/mac/clipboard_capture.cpp
    apps/desktop/src/wordselect/mac/hotkey_manager.cpp
    apps/desktop/src/wordselect/mac/platform_utils.cpp
    apps/desktop/src/wordselect/mac/platform_utils.h
)

add_executable(QTrans ${QTRANS_SOURCES_COMMON})

target_link_libraries(QTrans PRIVATE qtrans_engine Qt6::Widgets)

if(WIN32)
    target_sources(QTrans PRIVATE ${QTRANS_SOURCES_WIN} apps/desktop/src/app/win/single_instance_activate.cpp)
elseif(APPLE)
    target_sources(QTrans PRIVATE ${QTRANS_SOURCES_MAC} apps/desktop/src/app/mac/single_instance.mm)
    target_link_libraries(QTrans PRIVATE "-framework AppKit" "-framework ApplicationServices" "-framework Carbon" "-framework CoreGraphics")
    qt_import_plugins(QTrans
        INCLUDE BY_TYPE platforms Qt6::QCocoaIntegrationPlugin
        INCLUDE BY_TYPE platformthemes Qt6::QMacStylePlugin
    )
endif()

if(MINGW)
    target_link_options(QTrans PRIVATE -static-libgcc -static-libstdc++ -static -mwindows)
endif()

if(QTRANS_BUILD_TESTS)
    add_subdirectory(tests)
endif()

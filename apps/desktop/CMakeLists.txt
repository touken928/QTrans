add_library(qtrans_engine STATIC
    src/storage/app_paths.cpp
    src/storage/settings.cpp
    src/inference/inference_backend.cpp
    src/inference/inference_resolver.cpp
    src/catalog/model_catalog.cpp
    src/models/local/hymt2_18b_local_model.cpp
    src/models/local/hymt2_7b_local_model.cpp
    src/models/local/local_model_factory.cpp
    src/catalog/language_list.cpp
    src/inference/platform_profile.cpp
    src/inference/runtime_capabilities.cpp
    src/services/download/download.cpp
    src/services/engine/inference_engine.cpp
    src/services/task/task_queue.cpp
    src/services/task/task_orchestrator.cpp
    src/log/init.cpp
    src/log/logger.cpp
    src/log/registry.cpp
    src/log/component.cpp
    src/log/ai_trace.cpp
    src/log/console_progress.cpp
)

target_include_directories(qtrans_engine PUBLIC src)
target_link_libraries(qtrans_engine PUBLIC qtrans_core)

target_link_libraries(qtrans_engine PUBLIC
    llama
    CURL::libcurl
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

# ── QTrans executable ────────────────────────────────────────────────────────

set(_QT_SOURCES_COMMON
    src/ui/theme.h
    src/app/string_bridge.cpp
    src/app/main.cpp
    src/app/single_instance.cpp
    src/app/single_instance.h
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/app/task_service.cpp
    src/app/task_service.h
    src/ui/modal_overlay.cpp
    src/ui/modal_overlay.h
    src/ui/widgets/alert_panel.cpp
    src/ui/widgets/alert_panel.h
    src/ui/widgets/app_theme.cpp
    src/ui/widgets/app_theme.h
    src/ui/widgets/download_progress_panel.cpp
    src/ui/widgets/download_progress_panel.h
    src/ui/widgets/image_utils.cpp
    src/ui/widgets/image_utils.h
    src/ui/widgets/model_missing_panel.cpp
    src/ui/widgets/model_missing_panel.h
    src/ui/widgets/model_page.cpp
    src/ui/widgets/model_page.h
    src/ui/widgets/sidebar_widget.cpp
    src/ui/widgets/sidebar_widget.h
    src/ui/widgets/translate_page.cpp
    src/ui/widgets/translate_page.h
    src/ui/widgets/wordselect_page.cpp
    src/ui/widgets/wordselect_page.h
    src/ui/widget_utils.cpp
    src/ui/widget_utils.h
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

set(_QT_SOURCES_WIN
    src/wordselect/win/clipboard_capture.cpp
    src/wordselect/win/hotkey_manager.cpp
)

set(_QT_SOURCES_MAC
    src/wordselect/mac/clipboard_capture.cpp
    src/wordselect/mac/hotkey_manager.cpp
    src/wordselect/mac/platform_utils.cpp
    src/wordselect/mac/platform_utils.h
)

add_executable(QTrans ${_QT_SOURCES_COMMON})

target_link_libraries(QTrans PRIVATE qtrans_engine Qt6::Widgets)

if(WIN32)
    target_sources(QTrans PRIVATE ${_QT_SOURCES_WIN} src/app/win/single_instance_activate.cpp)
elseif(APPLE)
    target_sources(QTrans PRIVATE ${_QT_SOURCES_MAC} src/app/mac/single_instance.mm)
    target_link_libraries(QTrans PRIVATE "-framework AppKit" "-framework ApplicationServices" "-framework Carbon" "-framework CoreGraphics")
    qt_import_plugins(QTrans
        INCLUDE BY_TYPE platforms Qt6::QCocoaIntegrationPlugin
        INCLUDE BY_TYPE platformthemes Qt6::QMacStylePlugin
    )
endif()

if(MINGW)
    target_link_options(QTrans PRIVATE -static-libgcc -static-libstdc++ -static -mwindows)
endif()

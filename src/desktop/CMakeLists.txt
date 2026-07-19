add_library(qtrans_desktop STATIC
    domain/storage/app_paths.cpp
    domain/settings/settings.cpp
    domain/inference/inference_resolver.cpp
    domain/model-catalog/model_catalog.cpp
    domain/model-adapters/hymt2_18b_local_model.cpp
    domain/model-adapters/hymt2_7b_local_model.cpp
    domain/model-adapters/local_model_factory.cpp
    domain/model-catalog/language_list.cpp
    domain/inference/platform_profile.cpp
    domain/inference/runtime_capabilities.cpp
    domain/download/download.cpp
    domain/download/model_downloader.cpp
    domain/inference/engine/inference_engine.cpp
    domain/inference/production_translation_session.cpp
    domain/tasks/task_queue.cpp
    domain/tasks/task_orchestrator.cpp
    domain/logging/init.cpp
    domain/logging/logger.cpp
    domain/logging/registry.cpp
    domain/logging/component.cpp
    domain/logging/ai_trace.cpp
    domain/logging/console_progress.cpp
    domain/batch/batch_store.cpp
    domain/batch/batch_file_handler.cpp
)

target_include_directories(qtrans_desktop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qtrans_desktop PUBLIC qtrans_core)

target_link_libraries(qtrans_desktop PUBLIC spdlog::spdlog)

target_compile_definitions(qtrans_desktop PUBLIC
    $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_WARN>
)

if(QTRANS_MULTI_BACKEND_VALUE)
    target_compile_definitions(qtrans_desktop PUBLIC QTRANS_MULTI_BACKEND)
endif()

if(QTRANS_GPU_METAL_VALUE)
    target_compile_definitions(qtrans_desktop PUBLIC QTRANS_GPU_METAL)
endif()

# ── QTrans executable ────────────────────────────────────────────────────────

set(_QT_SOURCES_COMMON
    ui/shared/theme/theme.h
    shared/string_bridge.cpp
    app/main.cpp
    domain/platform/single_instance/single_instance.cpp
    domain/platform/single_instance/single_instance.h
    ui/shell/mainwindow.cpp
    ui/shell/mainwindow.h
    app/task_service.cpp
    app/task_service.h
    app/batch_controller.cpp
    app/batch_controller.h
    ui/shared/modal_overlay.cpp
    ui/shared/modal_overlay.h
    ui/shared/panels/alert_panel.cpp
    ui/shared/panels/alert_panel.h
    ui/shared/theme/app_theme.cpp
    ui/shared/theme/app_theme.h
    ui/shared/panels/download_progress_panel.cpp
    ui/shared/panels/download_progress_panel.h
    ui/shared/media/image_utils.cpp
    ui/shared/media/image_utils.h
    ui/shared/panels/model_missing_panel.cpp
    ui/shared/panels/model_missing_panel.h
    ui/pages/models/model_page.cpp
    ui/pages/models/model_page.h
    ui/sidebar/sidebar_widget.cpp
    ui/sidebar/sidebar_widget.h
    ui/pages/translate/translate_page.cpp
    ui/pages/translate/translate_page.h
    ui/pages/wordselect/wordselect_page.cpp
    ui/pages/wordselect/wordselect_page.h
    ui/pages/batch/batch_page.cpp
    ui/pages/batch/batch_page.h
    ui/pages/batch/batch_card.cpp
    ui/pages/batch/batch_card.h
    ui/pages/batch/batch_lang_panel.cpp
    ui/pages/batch/batch_lang_panel.h
    ui/shared/widget_utils.cpp
    ui/shared/widget_utils.h
    ui/popup/popup_window.cpp
    ui/popup/popup_window.h
    domain/platform/clipboard/clipboard_capture.cpp
    domain/platform/clipboard/clipboard_capture.h
    domain/platform/hotkeys/hotkey_manager.h
    ui/popup/session_controller.cpp
    ui/popup/session_controller.h
    ui/popup/system_tray.cpp
    ui/popup/system_tray.h
    resources/qtrans.qrc
)

set(_QT_SOURCES_WIN
    domain/platform/win/clipboard_capture.cpp
    domain/platform/win/hotkey_manager.cpp
)

set(_QT_SOURCES_MAC
    domain/platform/mac/clipboard_capture.cpp
    domain/platform/mac/hotkey_manager.cpp
    domain/platform/mac/platform_utils.cpp
    domain/platform/mac/platform_utils.h
)

add_executable(QTrans ${_QT_SOURCES_COMMON})

target_link_libraries(QTrans PRIVATE qtrans_desktop Qt6::Widgets)

if(WIN32)
    target_sources(QTrans PRIVATE ${_QT_SOURCES_WIN} domain/platform/single_instance/win/single_instance_activate.cpp)
elseif(APPLE)
    target_sources(QTrans PRIVATE ${_QT_SOURCES_MAC} domain/platform/single_instance/mac/single_instance.mm)
    target_link_libraries(QTrans PRIVATE "-framework AppKit" "-framework ApplicationServices" "-framework Carbon" "-framework CoreGraphics")
    qt_import_plugins(QTrans
        INCLUDE BY_TYPE platforms Qt6::QCocoaIntegrationPlugin
        INCLUDE BY_TYPE platformthemes Qt6::QMacStylePlugin
    )
endif()

if(MINGW)
    target_link_options(QTrans PRIVATE -static-libgcc -static-libstdc++ -static -mwindows)
endif()

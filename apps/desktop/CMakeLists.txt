add_library(qtrans_engine STATIC
    src/domain/storage/app_paths.cpp
    src/domain/settings/settings.cpp
    src/domain/inference/inference_resolver.cpp
    src/domain/model-catalog/model_catalog.cpp
    src/domain/model-adapters/hymt2_18b_local_model.cpp
    src/domain/model-adapters/hymt2_7b_local_model.cpp
    src/domain/model-adapters/local_model_factory.cpp
    src/domain/model-catalog/language_list.cpp
    src/domain/inference/platform_profile.cpp
    src/domain/inference/runtime_capabilities.cpp
    src/domain/download/download.cpp
    src/domain/inference/engine/inference_engine.cpp
    src/domain/tasks/task_queue.cpp
    src/domain/tasks/task_orchestrator.cpp
    src/domain/logging/init.cpp
    src/domain/logging/logger.cpp
    src/domain/logging/registry.cpp
    src/domain/logging/component.cpp
    src/domain/logging/ai_trace.cpp
    src/domain/logging/console_progress.cpp
    src/domain/batch/batch_store.cpp
    src/domain/batch/batch_file_handler.cpp
)

target_include_directories(qtrans_engine PUBLIC src)
target_link_libraries(qtrans_engine PUBLIC qtrans_core)

target_link_libraries(qtrans_engine PUBLIC spdlog::spdlog)

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
    src/ui/shared/theme/theme.h
    src/shared/string_bridge.cpp
    src/app/main.cpp
    src/domain/platform/single_instance/single_instance.cpp
    src/domain/platform/single_instance/single_instance.h
    src/ui/shell/mainwindow.cpp
    src/ui/shell/mainwindow.h
    src/app/task_service.cpp
    src/app/task_service.h
    src/app/batch_controller.cpp
    src/app/batch_controller.h
    src/ui/shared/modal_overlay.cpp
    src/ui/shared/modal_overlay.h
    src/ui/shared/panels/alert_panel.cpp
    src/ui/shared/panels/alert_panel.h
    src/ui/shared/theme/app_theme.cpp
    src/ui/shared/theme/app_theme.h
    src/ui/shared/panels/download_progress_panel.cpp
    src/ui/shared/panels/download_progress_panel.h
    src/ui/shared/media/image_utils.cpp
    src/ui/shared/media/image_utils.h
    src/ui/shared/panels/model_missing_panel.cpp
    src/ui/shared/panels/model_missing_panel.h
    src/ui/pages/models/model_page.cpp
    src/ui/pages/models/model_page.h
    src/ui/sidebar/sidebar_widget.cpp
    src/ui/sidebar/sidebar_widget.h
    src/ui/pages/translate/translate_page.cpp
    src/ui/pages/translate/translate_page.h
    src/ui/pages/wordselect/wordselect_page.cpp
    src/ui/pages/wordselect/wordselect_page.h
    src/ui/pages/batch/batch_page.cpp
    src/ui/pages/batch/batch_page.h
    src/ui/pages/batch/batch_card.cpp
    src/ui/pages/batch/batch_card.h
    src/ui/pages/batch/batch_lang_panel.cpp
    src/ui/pages/batch/batch_lang_panel.h
    src/ui/shared/widget_utils.cpp
    src/ui/shared/widget_utils.h
    src/ui/popup/popup_window.cpp
    src/ui/popup/popup_window.h
    src/domain/platform/clipboard/clipboard_capture.cpp
    src/domain/platform/clipboard/clipboard_capture.h
    src/domain/platform/hotkeys/hotkey_manager.h
    src/ui/popup/session_controller.cpp
    src/ui/popup/session_controller.h
    src/ui/popup/system_tray.cpp
    src/ui/popup/system_tray.h
    resources/qtrans.qrc
)

set(_QT_SOURCES_WIN
    src/domain/platform/win/clipboard_capture.cpp
    src/domain/platform/win/hotkey_manager.cpp
)

set(_QT_SOURCES_MAC
    src/domain/platform/mac/clipboard_capture.cpp
    src/domain/platform/mac/hotkey_manager.cpp
    src/domain/platform/mac/platform_utils.cpp
    src/domain/platform/mac/platform_utils.h
)

add_executable(QTrans ${_QT_SOURCES_COMMON})

target_link_libraries(QTrans PRIVATE qtrans_engine Qt6::Widgets)

if(WIN32)
    target_sources(QTrans PRIVATE ${_QT_SOURCES_WIN} src/domain/platform/single_instance/win/single_instance_activate.cpp)
elseif(APPLE)
    target_sources(QTrans PRIVATE ${_QT_SOURCES_MAC} src/domain/platform/single_instance/mac/single_instance.mm)
    target_link_libraries(QTrans PRIVATE "-framework AppKit" "-framework ApplicationServices" "-framework Carbon" "-framework CoreGraphics")
    qt_import_plugins(QTrans
        INCLUDE BY_TYPE platforms Qt6::QCocoaIntegrationPlugin
        INCLUDE BY_TYPE platformthemes Qt6::QMacStylePlugin
    )
endif()

if(MINGW)
    target_link_options(QTrans PRIVATE -static-libgcc -static-libstdc++ -static -mwindows)
endif()
